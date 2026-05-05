#include "GLBuffer.h"
#include "GLVertexArray.h"
#include "GLRenderDevice.h"
#include "../../include/GLStateManager.h"
#include <unordered_map>
#include <memory>
#include <iostream>
#include <chrono>

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
#ifndef __APPLE__

#endif


#ifndef glDepthMask
#define glDepthMask(x)
#endif

namespace sdk {
namespace video {
namespace rhi {

// --- Shadow State Management ---
struct ShadowState {
    bool blendEnabled = false;
    bool cullFaceEnabled = false;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
};
static ShadowState s_shadowState;

void GLPipelineState::apply() {
    if (desc.blendState.blendEnabled != s_shadowState.blendEnabled) {
        if (desc.blendState.blendEnabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        s_shadowState.blendEnabled = desc.blendState.blendEnabled;
    }

    if (desc.rasterizerState.cullFaceEnabled != s_shadowState.cullFaceEnabled) {
        if (desc.rasterizerState.cullFaceEnabled) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        s_shadowState.cullFaceEnabled = desc.rasterizerState.cullFaceEnabled;
    }

    if (desc.depthStencilState.depthTestEnabled != s_shadowState.depthTestEnabled) {
        if (desc.depthStencilState.depthTestEnabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        s_shadowState.depthTestEnabled = desc.depthStencilState.depthTestEnabled;
    }

    if (desc.depthStencilState.depthWriteEnabled != s_shadowState.depthWriteEnabled) {
        glDepthMask(desc.depthStencilState.depthWriteEnabled ? GL_TRUE : GL_FALSE);
        s_shadowState.depthWriteEnabled = desc.depthStencilState.depthWriteEnabled;
    }
}

// --- GLCommandBuffer ---
static std::unordered_map<uint32_t, GLuint> s_fboCache;

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
    if (s_fboCache.find(texId) == s_fboCache.end()) {
        GLuint fbo;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "VIDEO_SDK_ERROR_GPU_OUT_OF_MEMORY: Framebuffer status incomplete!" << std::endl;
        }

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

void GLCommandBuffer::bindPipelineState(std::shared_ptr<IPipelineState> pso) {
    auto glPipeline = std::dynamic_pointer_cast<GLPipelineState>(pso);
    if (glPipeline) {
        if (glPipeline->desc.shaderProgram) {
            auto glProg = static_cast<GLShaderProgram*>(glPipeline->desc.shaderProgram);
            glUseProgram(glProg->getGLHandle());
        }
        glPipeline->apply();
    } else {
        glUseProgram(0);
    }
}

void GLCommandBuffer::bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> resourceSet) {
    // Stub
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
#if defined(GL_ES_VERSION_3_1) || !defined(__APPLE__)
#ifndef __APPLE__
    extern void glDispatchCompute(GLuint, GLuint, GLuint) __attribute__((weak));
    if (glDispatchCompute) {
        glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
    }
#endif
#endif
}

void GLCommandBuffer::pipelineBarrier(BarrierType type) {
#if defined(GL_ES_VERSION_3_1) || !defined(__APPLE__)
#ifndef __APPLE__
    extern void glMemoryBarrier(unsigned int barriers) __attribute__((weak));
    if (glMemoryBarrier) {
        glMemoryBarrier(0xFFFFFFFF); // Full barrier for simple translation
    }
#endif
#endif
}

// --- GLRenderDevice ---

GLRenderDevice::~GLRenderDevice() {
    processDeferredDeletions();
    // In a real device we would destroy ResourcePool here
}

std::shared_ptr<ITexture> GLRenderDevice::createTexture(const TextureDesc& desc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, desc.width, desc.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // OOM tracking would go here
    size_t memCost = desc.width * desc.height * 4;
    // ...

    auto tex = std::make_shared<GLTexture>(id, desc.width, desc.height);
    tex->setOwnsHandle(true);
    return tex;
}

std::shared_ptr<ICommandBuffer> GLRenderDevice::createCommandBuffer() {
    return std::make_shared<GLCommandBuffer>();
}

void GLRenderDevice::submit(ICommandBuffer* cmdBuffer) {
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
    return std::make_shared<GLShaderResourceSet>();
}

std::shared_ptr<ITexture> GLRenderDevice::bindExternalHardwareBuffer(void* nativeBuffer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // EGLImageKHR -> glEGLImageTargetTexture2DOES -> GL_TEXTURE_2D
    // Stub for binding AHardwareBuffer
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, id);
    // ... logic for eglCreateImageKHR ...
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    auto tex = std::make_shared<GLTexture>(id, 1, 1);
    tex->setOwnsHandle(true);
    return tex;
}

void GLRenderDevice::removeFBOFromCache(uint32_t texId) {
    auto it = s_fboCache.find(texId);
    if (it != s_fboCache.end()) {
        GLuint fbo = it->second;
        glDeleteFramebuffers(1, &fbo);
        s_fboCache.erase(it);
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
