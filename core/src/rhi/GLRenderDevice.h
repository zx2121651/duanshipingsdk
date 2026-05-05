#pragma once
#include "../../include/rhi/IRenderDevice.h"
#include "../../include/rhi/ICommandBuffer.h"
#include "GLTexture.h"
#include "GLShaderProgram.h"
#include <mutex>
#include <queue>
#include <functional>
#include <atomic>

namespace sdk {
namespace video {
namespace rhi {

class GLPipelineState : public IPipelineState {
public:
    PipelineStateDesc desc;
    const PipelineStateDesc& getDesc() const override { return desc; }
    void apply();
};

class GLShaderResourceSet : public IShaderResourceSet {
public:
    // ...
};

class GLCommandBuffer : public ICommandBuffer {
public:
    GLCommandBuffer() = default;
    ~GLCommandBuffer() override = default;

    void begin() override;
    void end() override;

    void beginRenderPass(const RenderPassDescriptor& descriptor) override;
    void endRenderPass() override;

    void bindPipelineState(std::shared_ptr<IPipelineState> pso) override;
    void bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> resourceSet) override;
    void bindVertexArray(IVertexArray* vao) override;

    void draw(uint32_t count) override;
    void drawIndexed(uint32_t indexCount) override;

    void pipelineBarrier(BarrierType type) override;
    void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) override;

private:
    uint32_t m_currentFBO = 0;
    int64_t m_beginTimeNs = 0;
};

class GLRenderDevice : public IRenderDevice {
public:
    GLRenderDevice() = default;
    ~GLRenderDevice() override;

    std::shared_ptr<ITexture> createTexture(const TextureDesc& desc) override;
    std::shared_ptr<IBuffer> createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data = nullptr) override;
    std::shared_ptr<IVertexArray> createVertexArray() override;

    std::shared_ptr<IPipelineState> createGraphicsPipeline(const PipelineStateDesc& desc) override;
    std::shared_ptr<IShaderResourceSet> createShaderResourceSet() override;

    std::shared_ptr<ICommandBuffer> createCommandBuffer() override;
    void submit(ICommandBuffer* cmdBuffer) override;

    std::shared_ptr<ITexture> bindExternalHardwareBuffer(void* nativeBuffer) override;

    static void removeFBOFromCache(uint32_t texId);

    void queueDeferredDeletion(std::function<void()> cleanupTask);
    void processDeferredDeletions();

private:
    std::mutex m_mutex;
    std::queue<std::function<void()>> m_deletionQueue;
    std::atomic<uint64_t> m_frameCount{0};
};

} // namespace rhi
} // namespace video
} // namespace sdk
