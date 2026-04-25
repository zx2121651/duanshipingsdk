#pragma once
#include "../../include/rhi/IRenderDevice.h"
#include "../../include/rhi/ICommandBuffer.h"
#include "GLTexture.h"

namespace sdk {
namespace video {
namespace rhi {

class GLCommandBuffer : public ICommandBuffer {
public:
    GLCommandBuffer() = default;
    ~GLCommandBuffer() override = default;

    void beginRenderPass(ITexture* outputTexture) override;
    void endRenderPass() override;
    void bindTexture(int slot, ITexture* texture) override;
    void draw(std::shared_ptr<IVertexArray> vao, int vertexCount) override;
    void drawIndexed(std::shared_ptr<IVertexArray> vao, int indexCount) override;
    void bindUniformBuffer(uint32_t bindingPoint, std::shared_ptr<IBuffer> ubo) override;

};

class GLRenderDevice : public IRenderDevice {
public:
    GLRenderDevice() = default;
    ~GLRenderDevice() override = default;

    std::shared_ptr<ITexture> createTexture(uint32_t width, uint32_t height) override;
    std::shared_ptr<IBuffer> createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data = nullptr) override;
    std::shared_ptr<IVertexArray> createVertexArray() override;

    std::shared_ptr<ICommandBuffer> createCommandBuffer() override;
    void submit(ICommandBuffer* cmdBuffer) override;
};

} // namespace rhi
} // namespace video
} // namespace sdk
