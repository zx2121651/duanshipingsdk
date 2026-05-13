#include "GLBuffer.h"
#include "GLVertexArray.h"
#include "GLRenderDevice.h"
#include "../../include/GLStateManager.h"
#include <memory>
#include <iostream>
#include <sstream>
#include <chrono>

#ifdef __ANDROID__
    #include <android/hardware_buffer.h>
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
    #include <GLES2/gl2ext.h>
    #include <GLES3/gl3.h>
    #include <GLES3/gl32.h>  // geometry + tessellation shader enums
#endif

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#elif !defined(__ANDROID__)
    #include <GLES3/gl3.h>
#endif

// We need an assertion macro
#define RHI_ASSERT(condition) if(!(condition)) { std::cerr << "RHI_ASSERT Failed: " << #condition << std::endl; std::abort(); }

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

#ifndef GL_BLEND
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#endif

#ifndef glDepthMask
#define glDepthMask(x)
#endif

#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif


// Helper: map BlendFactor enum to GL constant
static GLenum toGLBlendFactor(sdk::video::rhi::BlendFactor f) {
    using BF = sdk::video::rhi::BlendFactor;
    switch (f) {
        case BF::Zero:                 return GL_ZERO;
        case BF::One:                  return GL_ONE;
        case BF::SrcColor:             return GL_SRC_COLOR;
        case BF::OneMinusSrcColor:     return GL_ONE_MINUS_SRC_COLOR;
        case BF::DstColor:             return GL_DST_COLOR;
        case BF::OneMinusDstColor:     return GL_ONE_MINUS_DST_COLOR;
        case BF::SrcAlpha:             return GL_SRC_ALPHA;
        case BF::OneMinusSrcAlpha:     return GL_ONE_MINUS_SRC_ALPHA;
        case BF::DstAlpha:             return GL_DST_ALPHA;
        case BF::OneMinusDstAlpha:     return GL_ONE_MINUS_DST_ALPHA;
        case BF::ConstantAlpha:        return GL_CONSTANT_ALPHA;
        case BF::OneMinusConstantAlpha:return GL_ONE_MINUS_CONSTANT_ALPHA;
        default:                       return GL_ONE;
    }
}

namespace sdk {
namespace video {
namespace rhi {

static GLenum toGLBlendEquation(BlendEquation e) {
    switch (e) {
        case BlendEquation::Add:             return GL_FUNC_ADD;
        case BlendEquation::Subtract:        return GL_FUNC_SUBTRACT;
        case BlendEquation::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendEquation::Min:             return GL_MIN;
        case BlendEquation::Max:             return GL_MAX;
        default:                             return GL_FUNC_ADD;
    }
}

static GLenum toGLCompareFunc(CompareFunc f) {
    switch (f) {
        case CompareFunc::Never:        return GL_NEVER;
        case CompareFunc::Less:         return GL_LESS;
        case CompareFunc::Equal:        return GL_EQUAL;
        case CompareFunc::LessEqual:    return GL_LEQUAL;
        case CompareFunc::Greater:      return GL_GREATER;
        case CompareFunc::NotEqual:     return GL_NOTEQUAL;
        case CompareFunc::GreaterEqual: return GL_GEQUAL;
        case CompareFunc::Always:       return GL_ALWAYS;
        default:                        return GL_LESS;
    }
}

static GLenum toGLStencilOp(StencilOp op) {
    switch (op) {
        case StencilOp::Keep:          return GL_KEEP;
        case StencilOp::Zero:          return GL_ZERO;
        case StencilOp::Replace:       return GL_REPLACE;
        case StencilOp::Increment:     return GL_INCR;
        case StencilOp::Decrement:     return GL_DECR;
        case StencilOp::Invert:        return GL_INVERT;
        case StencilOp::IncrementWrap:  return GL_INCR_WRAP;
        case StencilOp::DecrementWrap:  return GL_DECR_WRAP;
        default:                       return GL_KEEP;
    }
}

static GLenum toGLFrontFace(bool ccw) {
    return ccw ? GL_CCW : GL_CW;
}

void GLPipelineState::apply(ShadowState& s) {
    // Blend enable/disable
    if (desc.blendState.blendEnabled != s.blendEnabled) {
        if (desc.blendState.blendEnabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        s.blendEnabled = desc.blendState.blendEnabled;
    }
    // Blend factors and equations
    if (desc.blendState.blendEnabled) {
        GLenum srcC = toGLBlendFactor(desc.blendState.srcColorFactor);
        GLenum dstC = toGLBlendFactor(desc.blendState.dstColorFactor);
        GLenum srcA = toGLBlendFactor(desc.blendState.srcAlphaFactor);
        GLenum dstA = toGLBlendFactor(desc.blendState.dstAlphaFactor);
        if (srcC != s.blendSrcColor || dstC != s.blendDstColor ||
            srcA != s.blendSrcAlpha || dstA != s.blendDstAlpha) {
            glBlendFuncSeparate(srcC, dstC, srcA, dstA);
            s.blendSrcColor = srcC; s.blendDstColor = dstC;
            s.blendSrcAlpha = srcA; s.blendDstAlpha = dstA;
        }
        glBlendEquationSeparate(toGLBlendEquation(desc.blendState.colorBlendEquation),
                                toGLBlendEquation(desc.blendState.alphaBlendEquation));
        glColorMask((desc.blendState.colorWriteMask & static_cast<uint32_t>(ColorWriteMask::Red))   != 0,
                    (desc.blendState.colorWriteMask & static_cast<uint32_t>(ColorWriteMask::Green)) != 0,
                    (desc.blendState.colorWriteMask & static_cast<uint32_t>(ColorWriteMask::Blue))  != 0,
                    (desc.blendState.colorWriteMask & static_cast<uint32_t>(ColorWriteMask::Alpha)) != 0);
    }
    // Cull face
    if (desc.rasterizerState.cullFaceEnabled != s.cullFaceEnabled) {
        if (desc.rasterizerState.cullFaceEnabled) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        s.cullFaceEnabled = desc.rasterizerState.cullFaceEnabled;
    }
    if (desc.rasterizerState.cullFaceEnabled) {
        glFrontFace(toGLFrontFace(desc.rasterizerState.frontFaceCCW));
    }
    // Depth bias
    if (desc.rasterizerState.depthBiasConstantFactor != 0.0f || desc.rasterizerState.depthBiasSlopeFactor != 0.0f) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(desc.rasterizerState.depthBiasSlopeFactor, desc.rasterizerState.depthBiasConstantFactor);
    }
    // Depth test
    if (desc.depthStencilState.depthTestEnabled != s.depthTestEnabled) {
        if (desc.depthStencilState.depthTestEnabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        s.depthTestEnabled = desc.depthStencilState.depthTestEnabled;
    }
    // Depth compare function
    if (desc.depthStencilState.depthTestEnabled) {
        glDepthFunc(toGLCompareFunc(desc.depthStencilState.depthCompareFunc));
    }
    // Depth write
    if (desc.depthStencilState.depthWriteEnabled != s.depthWriteEnabled) {
        glDepthMask(desc.depthStencilState.depthWriteEnabled ? GL_TRUE : GL_FALSE);
        s.depthWriteEnabled = desc.depthStencilState.depthWriteEnabled;
    }
    // Stencil
    if (desc.depthStencilState.stencilFront.stencilTestEnabled) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(toGLCompareFunc(desc.depthStencilState.stencilFront.compareFunc),
                      desc.depthStencilState.stencilFront.referenceValue,
                      desc.depthStencilState.stencilFront.compareMask);
        glStencilOp(toGLStencilOp(desc.depthStencilState.stencilFront.failOp),
                    toGLStencilOp(desc.depthStencilState.stencilFront.depthFailOp),
                    toGLStencilOp(desc.depthStencilState.stencilFront.passOp));
    } else {
        glDisable(GL_STENCIL_TEST);
    }
}

// --- GLShaderResourceSet ---

void GLShaderResourceSet::bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) {
    for (auto& b : m_bindings) {
        if (b.slot == slot) { b.texture = texture; return; }
    }
    m_bindings.push_back({slot, texture});
}

void GLShaderResourceSet::apply() {
    for (const auto& b : m_bindings) {
        glActiveTexture(GL_TEXTURE0 + b.slot);
        glBindTexture(GL_TEXTURE_2D, b.texture ? b.texture->getId() : 0);
    }
}

// --- GLCommandBuffer ---

void GLCommandBuffer::begin() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    m_beginTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

void GLCommandBuffer::end() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t endTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    int64_t elapsedNs = endTimeNs - m_beginTimeNs;
    if (elapsedNs > 2000000) { // 2ms
        std::cerr << "ALogW: ICommandBuffer::end() execution exceeded 2ms! Took " << (elapsedNs / 1000000.0f) << " ms" << std::endl;
    }
}

void GLCommandBuffer::setViewport(float x, float y, float width, float height) {
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
               static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void GLCommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(x), static_cast<GLint>(y),
              static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void GLCommandBuffer::beginRenderPass(const RenderPassDescriptor& descriptor) {
    if (descriptor.colorAttachments.empty() || !descriptor.colorAttachments[0].texture) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    auto outputTexture = descriptor.colorAttachments[0].texture;
    GLuint texId = outputTexture->getId();
    GLuint fbo = m_device
        ? m_device->getOrCreateFBO(texId, outputTexture->getWidth(), outputTexture->getHeight())
        : 0;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // Default viewport to full texture; caller may override with setViewport
    glViewport(0, 0, outputTexture->getWidth(), outputTexture->getHeight());
    m_currentFBO = fbo;

    if (descriptor.colorAttachments[0].loadAction == LoadAction::Clear) {
        Color c = descriptor.colorAttachments[0].clearColor;
        glClearColor(c.r, c.g, c.b, c.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void GLCommandBuffer::endRenderPass() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    m_currentFBO = 0;
}

void GLCommandBuffer::bindPipelineState(std::shared_ptr<IPipelineState> pso) {
    auto glPipeline = std::dynamic_pointer_cast<GLPipelineState>(pso);
    if (glPipeline) {
        if (glPipeline->desc.shaderProgram) {
            auto glProg = static_cast<GLShaderProgram*>(glPipeline->desc.shaderProgram);
            glUseProgram(glProg->getGLHandle());
        }
        static ShadowState s_fallbackShadow;
        ShadowState& shadow = m_device ? m_device->getShadowState() : s_fallbackShadow;
        glPipeline->apply(shadow);
        m_currentTopology = glPipeline->desc.primitiveTopology;
    } else {
        glUseProgram(0);
        m_currentTopology = PrimitiveTopology::TriangleStrip;
    }
}

void GLCommandBuffer::bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> resourceSet) {
    if (resourceSet) {
        resourceSet->apply();
    }
}

void GLCommandBuffer::bindVertexArray(IVertexArray* vao) {
    if (!vao) {
        glBindVertexArray(0);
        return;
    }
    auto glVao = static_cast<GLVertexArray*>(vao);
    glBindVertexArray(glVao->getGLHandle());
}

static GLenum toGLTopology(PrimitiveTopology t) {
    switch (t) {
        case PrimitiveTopology::PointList:     return GL_POINTS;
        case PrimitiveTopology::LineList:      return GL_LINES;
        case PrimitiveTopology::LineStrip:       return GL_LINE_STRIP;
        case PrimitiveTopology::TriangleList:    return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::TriangleFan:   return GL_TRIANGLE_FAN;
        default:                               return GL_TRIANGLE_STRIP;
    }
}

static GLenum toGLIndexType(IndexType t) {
    return (t == IndexType::UInt32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
}

void GLCommandBuffer::draw(uint32_t count) {
    glDrawArrays(toGLTopology(m_currentTopology), 0, count);
}

void GLCommandBuffer::drawIndexed(uint32_t indexCount, IndexType indexType) {
    glDrawElements(toGLTopology(m_currentTopology), indexCount, toGLIndexType(indexType), nullptr);
}

void GLCommandBuffer::dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) {
    // Compute dispatch is a command-buffer responsibility (not IShaderProgram's)
#if !defined(__APPLE__) && !defined(_MSC_VER)
    extern void glDispatchCompute(GLuint, GLuint, GLuint) __attribute__((weak));
    if (glDispatchCompute) {
        glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
    }
#else
    (void)numGroupsX; (void)numGroupsY; (void)numGroupsZ;
#endif
}

void GLCommandBuffer::pipelineBarrier(BarrierType type) {
#if !defined(__APPLE__) && !defined(_MSC_VER)
    GLbitfield bits = 0;
    switch (type) {
        case BarrierType::Pipeline:
            // Compute → fragment: texture/image reads need to see compute writes
            bits = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                 | GL_TEXTURE_FETCH_BARRIER_BIT
                 | GL_SHADER_STORAGE_BARRIER_BIT;
            break;
        case BarrierType::Memory:
        default:
            // Cross-stage general memory sync (e.g. UAV write then index buffer read)
            bits = GL_BUFFER_UPDATE_BARRIER_BIT
                 | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
                 | GL_ELEMENT_ARRAY_BARRIER_BIT
                 | GL_UNIFORM_BARRIER_BIT
                 | GL_TEXTURE_FETCH_BARRIER_BIT
                 | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                 | GL_FRAMEBUFFER_BARRIER_BIT
                 | GL_SHADER_STORAGE_BARRIER_BIT;
            break;
    }
    glMemoryBarrier(bits);
#elif defined(_MSC_VER)
    // glMemoryBarrier is a GLES 3.1 function; on mock/headless Windows, no-op.
    (void)type;
#endif
}

// --- GLRenderDevice ---

GLRenderDevice::~GLRenderDevice() {
    processDeferredDeletions();
    // In a real device we would destroy ResourcePool here
}

std::shared_ptr<ITexture> GLRenderDevice::createTexture(const TextureDesc& desc) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Resolve GL internal format, base format, and pixel type from TextureFormat
    GLenum internalFormat = GL_RGBA8;
    GLenum baseFormat     = GL_RGBA;
    GLenum pixelType      = GL_UNSIGNED_BYTE;
    GLenum texTarget      = GL_TEXTURE_2D;
    switch (desc.format) {
        case TextureFormat::RGBA8:
            internalFormat = GL_RGBA8; baseFormat = GL_RGBA; pixelType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::RGBA16F:
            internalFormat = GL_RGBA16F; baseFormat = GL_RGBA; pixelType = GL_HALF_FLOAT; break;
        case TextureFormat::RG16F:
            internalFormat = GL_RG16F;   baseFormat = GL_RG;   pixelType = GL_HALF_FLOAT; break;
        case TextureFormat::Depth24:
            internalFormat = GL_DEPTH_COMPONENT24; baseFormat = GL_DEPTH_COMPONENT; pixelType = GL_UNSIGNED_INT; break;
        case TextureFormat::RGB8:
            internalFormat = GL_RGB8;  baseFormat = GL_RGB;  pixelType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::NV12:
            // NV12: Y plane only; UV interleaved plane handled separately in RHI or via shader sampling
            internalFormat = GL_LUMINANCE8; baseFormat = GL_LUMINANCE; pixelType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::BGRA8:
            internalFormat = GL_BGRA8_EXT; baseFormat = GL_BGRA_EXT; pixelType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::R8:
            internalFormat = GL_R8; baseFormat = GL_RED; pixelType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::R16F:
            internalFormat = GL_R16F; baseFormat = GL_RED; pixelType = GL_HALF_FLOAT; break;
        case TextureFormat::R32F:
            internalFormat = GL_R32F; baseFormat = GL_RED; pixelType = GL_FLOAT; break;
        case TextureFormat::Depth32F:
            internalFormat = GL_DEPTH_COMPONENT32F; baseFormat = GL_DEPTH_COMPONENT; pixelType = GL_FLOAT; break;
        case TextureFormat::ASTC_4x4:
            internalFormat = GL_COMPRESSED_RGBA_ASTC_4x4_KHR; baseFormat = GL_RGBA; pixelType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::ASTC_6x6:
            internalFormat = GL_COMPRESSED_RGBA_ASTC_6x6_KHR; baseFormat = GL_RGBA; pixelType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::ASTC_8x8:
            internalFormat = GL_COMPRESSED_RGBA_ASTC_8x8_KHR; baseFormat = GL_RGBA; pixelType = GL_UNSIGNED_BYTE; break;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(texTarget, id);

    // For compressed formats use glCompressedTexImage2D, otherwise glTexImage2D
    bool isCompressed = (internalFormat == GL_COMPRESSED_RGBA_ASTC_4x4_KHR ||
                       internalFormat == GL_COMPRESSED_RGBA_ASTC_6x6_KHR ||
                       internalFormat == GL_COMPRESSED_RGBA_ASTC_8x8_KHR);

    if (isCompressed) {
        glCompressedTexImage2D(texTarget, 0, internalFormat, desc.width, desc.height, 0, 0, nullptr);
    } else {
        glTexImage2D(texTarget, 0, internalFormat, desc.width, desc.height, 0, baseFormat, pixelType, nullptr);
    }

    // Filter & wrap — min filter uses mip if >1 mip level
    GLenum minFilter = (desc.mipLevels > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(texTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(texTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Generate mipmaps if requested
    if (desc.mipLevels > 1) {
        glGenerateMipmap(texTarget);
    }

    glBindTexture(texTarget, 0);

    auto tex = std::make_shared<GLTexture>(id, desc.width, desc.height, desc.format, desc.mipLevels);
    tex->setOwnsHandle(true);
    return tex;
}

std::shared_ptr<ICommandBuffer> GLRenderDevice::createCommandBuffer() {
    return std::make_shared<GLCommandBuffer>(this);
}

std::shared_ptr<IShaderProgram> GLRenderDevice::createShaderProgram(
        const char* vertexSrc, const char* fragmentSrc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto prog = std::make_shared<GLShaderProgram>(vertexSrc, fragmentSrc);
    if (!prog->isValid()) {
        std::cerr << "RHI: createShaderProgram failed (see GLShaderProgram log)" << std::endl;
        return nullptr;
    }
    return prog;
}

// ---------------------------------------------------------------------------
// GLES 3.2 — 多着色器阶段程序编译辅助
// ---------------------------------------------------------------------------
static GLuint compileShaderStage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(len), '\0');
        glGetShaderInfoLog(s, len, nullptr, &log[0]);
        std::cerr << "RHI: shader compile error: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static std::shared_ptr<IShaderProgram> linkMultiStageProgram(
    std::initializer_list<GLuint> stages)
{
    GLuint prog = glCreateProgram();
    for (GLuint s : stages)
        if (s) glAttachShader(prog, s);
    glLinkProgram(prog);
    for (GLuint s : stages)
        if (s) { glDetachShader(prog, s); glDeleteShader(s); }
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(len), '\0');
        glGetProgramInfoLog(prog, len, nullptr, &log[0]);
        std::cerr << "RHI: multi-stage link error: " << log << std::endl;
        glDeleteProgram(prog);
        return nullptr;
    }
    return std::make_shared<GLShaderProgram>(prog);
}

std::shared_ptr<IShaderProgram> GLRenderDevice::createGeometryShaderProgram(
    const char* vertSrc, const char* geomSrc, const char* fragSrc)
{
#if defined(__ANDROID__) || defined(HAS_GLES32)
    GLuint v = compileShaderStage(GL_VERTEX_SHADER,   vertSrc);
    GLuint g = compileShaderStage(GL_GEOMETRY_SHADER, geomSrc);
    GLuint f = compileShaderStage(GL_FRAGMENT_SHADER, fragSrc);
    return linkMultiStageProgram({v, g, f});
#else
    std::cerr << "RHI: createGeometryShaderProgram — GL_GEOMETRY_SHADER not available on this platform" << std::endl;
    return nullptr;
#endif
}

std::shared_ptr<IShaderProgram> GLRenderDevice::createTessellationProgram(
    const char* vertSrc, const char* tescSrc, const char* teseSrc, const char* fragSrc)
{
#if defined(__ANDROID__) || defined(HAS_GLES32)
    GLuint v  = compileShaderStage(GL_VERTEX_SHADER,          vertSrc);
    GLuint tc = compileShaderStage(GL_TESS_CONTROL_SHADER,    tescSrc);
    GLuint te = compileShaderStage(GL_TESS_EVALUATION_SHADER, teseSrc);
    GLuint f  = compileShaderStage(GL_FRAGMENT_SHADER,        fragSrc);
    return linkMultiStageProgram({v, tc, te, f});
#else
    std::cerr << "RHI: createTessellationProgram — tessellation shaders not available on this platform" << std::endl;
    return nullptr;
#endif
}

std::shared_ptr<ITexture> GLRenderDevice::createMSAATexture(
    const TextureDesc& desc, int samples)
{
    // Clamp samples to a power of two between 1 and the GL maximum
    if (samples <= 1) return createTexture(desc);
    const int maxS = 16; // safe upper bound before querying
    if (samples > maxS) samples = maxS;

    GLuint id = 0;
    glGenTextures(1, &id);
#if defined(__ANDROID__) || defined(HAS_GLES32)
    // GLES 3.2: GL_TEXTURE_2D_MULTISAMPLE
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, id);

    // Map TextureFormat → GL internal format
    GLenum internalFmt = GL_RGBA8;
    if (desc.format == TextureFormat::RGBA16F) internalFmt = GL_RGBA16F;
    else if (desc.format == TextureFormat::RG16F)  internalFmt = GL_RG16F;

    // glTexStorage2DMultisample is GLES 3.1+ — call via function pointer on Android
    using PFN_glTexStorage2DMultisample =
        void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean);
#   ifdef __ANDROID__
    static auto fn = reinterpret_cast<PFN_glTexStorage2DMultisample>(
        eglGetProcAddress("glTexStorage2DMultisample"));
    if (fn) {
        fn(GL_TEXTURE_2D_MULTISAMPLE,
           static_cast<GLsizei>(samples),
           internalFmt,
           static_cast<GLsizei>(desc.width),
           static_cast<GLsizei>(desc.height),
           GL_TRUE);
    }
#   endif
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
#else
    // Fallback: regular (non-MSAA) texture on desktop/iOS
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    std::cerr << "RHI: createMSAATexture — MSAA not available, fallback to regular texture" << std::endl;
#endif
    auto tex = std::make_shared<GLTexture>(id, desc.width, desc.height, desc.format, 1);
    tex->setOwnsHandle(true);
    return tex;
}

void GLRenderDevice::submit(ICommandBuffer* cmdBuffer) {
    // Immediate-mode GL backend: commands are executed inline during recording.
    // glFlush() ensures all queued GL commands are submitted to the GPU driver
    // before this function returns — semantically equivalent to Vulkan queue submit.
    glFlush();
    m_frameCount.fetch_add(1, std::memory_order_relaxed);
    processDeferredDeletions();
}

std::shared_ptr<IBuffer> GLRenderDevice::createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::make_shared<GLBuffer>(type, usage, size, data);
}

std::shared_ptr<IVertexArray> GLRenderDevice::createVertexArray() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::make_shared<GLVertexArray>();
}

std::shared_ptr<IPipelineState> GLRenderDevice::createGraphicsPipeline(const PipelineStateDesc& desc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto pso = std::make_shared<GLPipelineState>();
    pso->desc = desc;
    return pso;
}

std::shared_ptr<IShaderResourceSet> GLRenderDevice::createShaderResourceSet() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::make_shared<GLShaderResourceSet>();  // Now has real bindTexture/apply impl
}

std::shared_ptr<ITexture> GLRenderDevice::bindExternalHardwareBuffer(void* nativeBuffer) {
    std::lock_guard<std::mutex> lock(m_mutex);

#if defined(__ANDROID__) && __ANDROID_API__ >= 26
    if (!nativeBuffer) return nullptr;

    AHardwareBuffer* ahwb = static_cast<AHardwareBuffer*>(nativeBuffer);

    AHardwareBuffer_Desc hwbDesc = {};
    AHardwareBuffer_describe(ahwb, &hwbDesc);

    EGLDisplay display = eglGetCurrentDisplay();
    if (display == EGL_NO_DISPLAY) {
        std::cerr << "bindExternalHardwareBuffer: No EGL display" << std::endl;
        return nullptr;
    }

    // Load EGL extension function pointers (extensions — not in core EGL)
    static auto s_eglGetNativeClientBufferANDROID =
        (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
    static auto s_eglCreateImageKHR =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    static auto s_eglDestroyImageKHR =
        (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    static auto s_glEGLImageTargetTexture2DOES =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if (!s_eglGetNativeClientBufferANDROID || !s_eglCreateImageKHR ||
        !s_eglDestroyImageKHR || !s_glEGLImageTargetTexture2DOES) {
        std::cerr << "bindExternalHardwareBuffer: required EGL extensions unavailable" << std::endl;
        return nullptr;
    }

    EGLClientBuffer clientBuffer = s_eglGetNativeClientBufferANDROID(ahwb);
    if (!clientBuffer) {
        std::cerr << "bindExternalHardwareBuffer: eglGetNativeClientBufferANDROID failed" << std::endl;
        return nullptr;
    }

    const EGLint imageAttribs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    EGLImageKHR eglImage = s_eglCreateImageKHR(
        display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, clientBuffer, imageAttribs);
    if (eglImage == EGL_NO_IMAGE_KHR) {
        std::cerr << "bindExternalHardwareBuffer: eglCreateImageKHR failed" << std::endl;
        return nullptr;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, id);
    s_glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    s_eglDestroyImageKHR(display, eglImage);

    auto tex = std::make_shared<GLTexture>(id, hwbDesc.width, hwbDesc.height, TextureFormat::BGRA8, 1);
    tex->setOwnsHandle(true);
    return tex;
#else
    // Non-Android stub
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, id);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    auto tex = std::make_shared<GLTexture>(id, 1, 1, TextureFormat::BGRA8, 1);
    tex->setOwnsHandle(true);
    return tex;
#endif
}

GLuint GLRenderDevice::getOrCreateFBO(uint32_t texId, uint32_t texWidth, uint32_t texHeight) {
    auto it = m_fboCache.find(texId);
    if (it != m_fboCache.end()) return it->second;

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "RHI: Framebuffer incomplete for texId=" << texId << std::endl;
    }

    m_fboCache[texId] = fbo;
    return fbo;
}

void GLRenderDevice::removeFBOFromCache(uint32_t texId) {
    auto it = m_fboCache.find(texId);
    if (it != m_fboCache.end()) {
        GLuint fbo = it->second;
        glDeleteFramebuffers(1, &fbo);
        m_fboCache.erase(it);
    }
}

void GLRenderDevice::queueDeferredDeletion(std::function<void()> cleanupTask) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_deletionQueue.push(cleanupTask);
}

void GLRenderDevice::processDeferredDeletions() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while(!m_deletionQueue.empty()) {
        m_deletionQueue.front()();
        m_deletionQueue.pop();
    }
}

RHICapabilities GLRenderDevice::getCapabilities() const {
    RHICapabilities caps;
    caps.backend = BackendType::GLES;

    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    caps.rendererString = renderer ? renderer : "";

    // Parse GLES version (e.g. "OpenGL ES 3.1 ...")
    const char* versionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    int major = 3, minor = 0;
    if (versionStr) {
        std::string vs(versionStr);
        size_t pos = vs.find("OpenGL ES ");
        if (pos != std::string::npos) {
            std::stringstream ss(vs.substr(pos + 10));
            char dot;
            ss >> major >> dot >> minor;
        }
    }
    caps.glesVersionInt = major * 10 + minor; // 30, 31, 32

    // Geometry shader: GLES 3.2 core
    caps.geometryShader = (major > 3 || (major == 3 && minor >= 2));
    // Tessellation: GLES 3.2 core
    caps.tessellation = caps.geometryShader;

    // MSAA
    glGetIntegerv(GL_MAX_SAMPLES, &caps.maxMSAASamples);
    caps.msaa = (caps.maxMSAASamples >= 4);

    // Compute shader: GLES 3.1+ with sufficient invocations
#ifdef __ANDROID__
    if (major > 3 || (major == 3 && minor >= 1)) {
        GLint maxInv = 0;
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxInv);
        caps.computeShader = (maxInv >= 256);
    }
#endif

    // FP16 render target
    auto hasExt = [&](const char* name) -> bool {
        GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; ++i) {
            const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
            if (e && std::string(e) == name) return true;
        }
        return false;
    };
    caps.fp16RenderTarget = (major >= 3 && minor >= 2)
        || hasExt("GL_OES_texture_half_float")
        || hasExt("GL_EXT_color_buffer_half_float");

    // ASTC
    caps.astc = (major >= 3 && minor >= 2) || hasExt("GL_KHR_texture_compression_astc_ldr");

    return caps;
}

} // namespace rhi
} // namespace video
} // namespace sdk
