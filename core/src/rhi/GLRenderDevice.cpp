#include "GLBuffer.h"
#include "GLVertexArray.h"
#include "GLRenderDevice.h"
#include "../../include/GLStateManager.h"
#include <memory>
#include <iostream>
#include <chrono>

#ifdef __ANDROID__
    #include <android/hardware_buffer.h>
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
    #include <GLES2/gl2ext.h>
#endif

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
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

void GLPipelineState::apply(ShadowState& s) {
    // Blend enable/disable
    if (desc.blendState.blendEnabled != s.blendEnabled) {
        if (desc.blendState.blendEnabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        s.blendEnabled = desc.blendState.blendEnabled;
    }
    // Blend factors (only update if blend is active to avoid spurious state)
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
    }
    // Cull face
    if (desc.rasterizerState.cullFaceEnabled != s.cullFaceEnabled) {
        if (desc.rasterizerState.cullFaceEnabled) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        s.cullFaceEnabled = desc.rasterizerState.cullFaceEnabled;
    }
    // Depth test
    if (desc.depthStencilState.depthTestEnabled != s.depthTestEnabled) {
        if (desc.depthStencilState.depthTestEnabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        s.depthTestEnabled = desc.depthStencilState.depthTestEnabled;
    }
    // Depth write
    if (desc.depthStencilState.depthWriteEnabled != s.depthWriteEnabled) {
        glDepthMask(desc.depthStencilState.depthWriteEnabled ? GL_TRUE : GL_FALSE);
        s.depthWriteEnabled = desc.depthStencilState.depthWriteEnabled;
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
    m_currentFBO = 0;
}

void GLCommandBuffer::bindPipelineState(std::shared_ptr<IPipelineState> pso) {
    auto glPipeline = std::dynamic_pointer_cast<GLPipelineState>(pso);
    if (glPipeline) {
        if (glPipeline->desc.shaderProgram) {
            auto glProg = static_cast<GLShaderProgram*>(glPipeline->desc.shaderProgram);
            glUseProgram(glProg->getGLHandle());
        }
        static ShadowState s_fallbackShadow; // fallback for device-less command buffers
        ShadowState& shadow = m_device ? m_device->getShadowState() : s_fallbackShadow;
        glPipeline->apply(shadow);
    } else {
        glUseProgram(0);
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

void GLCommandBuffer::draw(uint32_t count) {
    glDrawArrays(GL_TRIANGLE_STRIP, 0, count);
}

void GLCommandBuffer::drawIndexed(uint32_t indexCount) {
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
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
    extern void glMemoryBarrier(unsigned int barriers) __attribute__((weak));
    if (glMemoryBarrier) {
        glMemoryBarrier(0xFFFFFFFF);
    }
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
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, desc.width, desc.height, 0, baseFormat, pixelType, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    auto tex = std::make_shared<GLTexture>(id, desc.width, desc.height);
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
    auto tex = std::make_shared<GLTexture>(id, desc.width, desc.height);
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

#ifdef __ANDROID__
    if (!nativeBuffer) return nullptr;

    AHardwareBuffer* ahwb = static_cast<AHardwareBuffer*>(nativeBuffer);

    AHardwareBuffer_Desc hwbDesc = {};
    AHardwareBuffer_describe(ahwb, &hwbDesc);

    EGLDisplay display = eglGetCurrentDisplay();
    if (display == EGL_NO_DISPLAY) {
        std::cerr << "bindExternalHardwareBuffer: No EGL display" << std::endl;
        return nullptr;
    }

    EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(ahwb);
    if (!clientBuffer) {
        std::cerr << "bindExternalHardwareBuffer: eglGetNativeClientBufferANDROID failed" << std::endl;
        return nullptr;
    }

    const EGLint imageAttribs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    EGLImageKHR eglImage = eglCreateImageKHR(
        display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, clientBuffer, imageAttribs);
    if (eglImage == EGL_NO_IMAGE_KHR) {
        std::cerr << "bindExternalHardwareBuffer: eglCreateImageKHR failed" << std::endl;
        return nullptr;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, id);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    eglDestroyImageKHR(display, eglImage);

    // GLTexture does NOT own the handle here — AHardwareBuffer manages the backing memory
    auto tex = std::make_shared<GLTexture>(id, hwbDesc.width, hwbDesc.height);
    tex->setOwnsHandle(true);  // We do own the GL texture object (not the HW buffer)
    return tex;
#else
    // Non-Android stub
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, id);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    auto tex = std::make_shared<GLTexture>(id, 1, 1);
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

} // namespace rhi
} // namespace video
} // namespace sdk
