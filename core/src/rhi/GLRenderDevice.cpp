#include "GLBuffer.h"
#include "GLVertexArray.h"
#include "GLRenderDevice.h"
#include "GLShaderProgram.h"
#include "../../include/GLStateManager.h"
#include <unordered_map>
#include <memory>

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA
#endif

#ifndef GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#define GL_ELEMENT_ARRAY_BARRIER_BIT 0x00000002
#define GL_UNIFORM_BARRIER_BIT 0x00000004
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif

#ifndef GLbitfield
typedef unsigned int GLbitfield;
#endif

#ifndef __APPLE__
extern "C" {
    void glBindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format) __attribute__((weak));
    void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z) __attribute__((weak));
    void glMemoryBarrier(GLbitfield barriers) __attribute__((weak));
}
#endif

namespace sdk {
namespace video {
namespace rhi {

// --- GLCommandBuffer ---

static std::unordered_map<uint32_t, GLuint> s_fboCache;















void GLCommandBuffer::dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) {
#if defined(GL_ES_VERSION_3_1) || !defined(__APPLE__)
#ifndef __APPLE__
    if (glDispatchCompute) {
        glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
    }
#endif
#endif
}

void GLCommandBuffer::memoryBarrier(uint32_t barriers) {
#if defined(GL_ES_VERSION_3_1) || !defined(__APPLE__)
    GLbitfield glBarriers = 0;
    if (barriers & static_cast<uint32_t>(BarrierBit::VertexAttribArray)) glBarriers |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
    if (barriers & static_cast<uint32_t>(BarrierBit::ElementArray)) glBarriers |= GL_ELEMENT_ARRAY_BARRIER_BIT;
    if (barriers & static_cast<uint32_t>(BarrierBit::Uniform)) glBarriers |= GL_UNIFORM_BARRIER_BIT;
    if (barriers & static_cast<uint32_t>(BarrierBit::TextureFetch)) glBarriers |= GL_TEXTURE_FETCH_BARRIER_BIT;
    if (barriers & static_cast<uint32_t>(BarrierBit::ShaderImageAccess)) glBarriers |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;

#ifndef __APPLE__
    if (glMemoryBarrier) {
        glMemoryBarrier(glBarriers);
    }
#endif
#endif
}

// --- GLRenderDevice ---

std::shared_ptr<ITexture> GLRenderDevice::createTexture(uint32_t width, uint32_t height) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    auto tex = std::make_shared<GLTexture>(id, width, height);
    tex->setOwnsHandle(true);
    return tex;
}


void GLCommandBuffer::beginRenderPass(const RenderPassDescriptor& descriptor) {
    if (descriptor.colorAttachments.empty() || !descriptor.colorAttachments[0].texture) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    auto outputTexture = descriptor.colorAttachments[0].texture;
    GLuint texId = outputTexture->getId();
    if (s_fboCache.find(texId) == s_fboCache.end()) {
        GLuint fbo;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);
        s_fboCache[texId] = fbo;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, s_fboCache[texId]);
    }

    glViewport(0, 0, outputTexture->getWidth(), outputTexture->getHeight());
    m_currentFBO = s_fboCache[texId];

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

void GLCommandBuffer::bindPipeline(std::shared_ptr<IPipelineState> pipeline) {
    auto glPipeline = std::dynamic_pointer_cast<GLPipelineState>(pipeline);
    if (glPipeline && glPipeline->desc.shaderProgram) {
        auto glProg = std::dynamic_pointer_cast<GLShaderProgram>(glPipeline->desc.shaderProgram);
        if (glProg) glUseProgram(glProg->getGLHandle());
    } else {
        glUseProgram(0);
    }
}

void GLCommandBuffer::bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> resourceSet) {
    // Stub
}

void GLCommandBuffer::bindVertexArray(std::shared_ptr<IVertexArray> vao) {
    if (!vao) {
        glBindVertexArray(0);
        return;
    }
    auto glVao = std::static_pointer_cast<GLVertexArray>(vao);
    glBindVertexArray(glVao->getGLHandle());
}

void GLCommandBuffer::draw(int vertexCount, int instanceCount) {
    glDrawArrays(GL_TRIANGLE_STRIP, 0, vertexCount);
}

void GLCommandBuffer::drawIndexed(int indexCount, int instanceCount) {
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
}

std::shared_ptr<IPipelineState> GLRenderDevice::createGraphicsPipeline(const GraphicsPipelineDescriptor& desc) {
    auto pso = std::make_shared<GLPipelineState>();
    pso->desc = desc;
    return pso;
}

std::shared_ptr<IShaderResourceSet> GLRenderDevice::createShaderResourceSet() {
    return std::make_shared<GLShaderResourceSet>();
}

std::shared_ptr<ICommandBuffer> GLRenderDevice::createCommandBuffer() {
    return std::make_shared<GLCommandBuffer>();
}

void GLRenderDevice::submit(ICommandBuffer* cmdBuffer) {
#ifndef MOCK_GL_ENV_VAR
#endif
}

std::shared_ptr<IBuffer> GLRenderDevice::createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data) {
    return std::make_shared<GLBuffer>(type, usage, size, data);
}

std::shared_ptr<IVertexArray> GLRenderDevice::createVertexArray() {
    return std::make_shared<GLVertexArray>();
}

} // namespace rhi
} // namespace video
} // namespace sdk
