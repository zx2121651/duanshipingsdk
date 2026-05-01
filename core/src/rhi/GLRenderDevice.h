#pragma once
#include "../../include/rhi/IRenderDevice.h"
#include "../../include/rhi/ICommandBuffer.h"
#include "GLTexture.h"

namespace sdk {
namespace video {
namespace rhi {


class GLPipelineState : public IPipelineState {
public:
    GraphicsPipelineDescriptor desc;
};

class GLShaderResourceSet : public IShaderResourceSet {
public:
    // ...
};

class GLCommandBuffer : public ICommandBuffer {
public:
    GLCommandBuffer() = default;
    ~GLCommandBuffer() override = default;

    void beginRenderPass(const RenderPassDescriptor& descriptor) override;
    void endRenderPass() override;

    void bindPipeline(std::shared_ptr<IPipelineState> pipeline) override;
    void bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> resourceSet) override;
    void bindVertexArray(std::shared_ptr<IVertexArray> vao) override;

    void draw(int vertexCount, int instanceCount = 1) override;
    void drawIndexed(int indexCount, int instanceCount = 1) override;

    void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) override;
    void memoryBarrier(uint32_t barriers) override;

private:
    uint32_t m_currentFBO = 0;
};


class GLRenderDevice : public IRenderDevice {
public:
    std::shared_ptr<IPipelineState> createGraphicsPipeline(const GraphicsPipelineDescriptor& desc) override;
    std::shared_ptr<IShaderResourceSet> createShaderResourceSet() override;

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
