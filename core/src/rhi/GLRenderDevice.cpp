#include "GLBuffer.h"
#include "GLVertexArray.h"
#include "GLVertexArray.h"
#include "GLBuffer.h"
#include "GLRenderDevice.h"
#include "../../include/GLStateManager.h"

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {
namespace rhi {

// --- GLCommandBuffer ---

void GLCommandBuffer::beginRenderPass(ITexture* outputTexture) {
    // In a real RHI, this would bind the FBO.
    // For this transitional phase, we assume the FBO is bound externally or by FrameBuffer::bind(),
    // but we can enforce viewport here if needed.
}

void GLCommandBuffer::endRenderPass() {
    // Cleanup if necessary
}

void GLCommandBuffer::bindTexture(int slot, ITexture* texture) {
    if (!texture) return;
    GLenum glSlot = GL_TEXTURE0 + slot;
    GLStateManager::getInstance().activeTexture(glSlot);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, texture->getId());
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

std::shared_ptr<ICommandBuffer> GLRenderDevice::createCommandBuffer() {
    return std::make_shared<GLCommandBuffer>();
}

void GLRenderDevice::submit(ICommandBuffer* cmdBuffer) {
    // In pure OpenGL, commands are executed immediately as they are recorded.
    // So "submit" might just be a flush or no-op in this backend.
#ifndef MOCK_GL_ENV_VAR
    // Mocks do not implement glFlush
#endif
}


std::shared_ptr<IBuffer> GLRenderDevice::createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data) {
    return std::make_shared<GLBuffer>(type, usage, size, data);
}

std::shared_ptr<IVertexArray> GLRenderDevice::createVertexArray() {
    return std::make_shared<GLVertexArray>();
}

void GLCommandBuffer::draw(std::shared_ptr<IVertexArray> vao, int vertexCount) {
    if (!vao) return;
    auto glVao = std::static_pointer_cast<GLVertexArray>(vao);
    glBindVertexArray(glVao->getGLHandle());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, vertexCount);
    glBindVertexArray(0);
}

void GLCommandBuffer::drawIndexed(std::shared_ptr<IVertexArray> vao, int indexCount) {
    if (!vao) return;
    auto glVao = std::static_pointer_cast<GLVertexArray>(vao);
    glBindVertexArray(glVao->getGLHandle());
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
}

void GLCommandBuffer::bindUniformBuffer(uint32_t bindingPoint, std::shared_ptr<IBuffer> ubo) {
    if (!ubo || ubo->getType() != BufferType::UniformBuffer) return;
    auto glBuffer = std::static_pointer_cast<GLBuffer>(ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, glBuffer->getGLHandle());
}
} // namespace rhi
} // namespace video
} // namespace sdk
