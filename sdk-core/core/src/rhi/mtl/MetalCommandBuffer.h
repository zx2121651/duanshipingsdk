#pragma once
#ifdef HAS_METAL

#include "../../../include/rhi/ICommandBuffer.h"
#include "MetalPipeline.h"

#ifdef __OBJC__
#   import <Metal/Metal.h>
    using MTLCommandBufferRef       = id<MTLCommandBuffer>;
    using MTLRenderCommandEncoderRef= id<MTLRenderCommandEncoder>;
    using MTLComputeCommandEncoderRef=id<MTLComputeCommandEncoder>;
#else
    using MTLCommandBufferRef        = void*;
    using MTLRenderCommandEncoderRef = void*;
    using MTLComputeCommandEncoderRef= void*;
#endif

namespace sdk {
namespace video {
namespace rhi {

class MetalRenderDevice;

class MetalCommandBuffer : public ICommandBuffer {
public:
    explicit MetalCommandBuffer(MetalRenderDevice* device);
    ~MetalCommandBuffer() override = default;

    void begin() override;
    void end()   override;

    void beginRenderPass(const RenderPassDescriptor& desc) override;
    void endRenderPass()  override;

    void setViewport(float x, float y, float width, float height) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void bindPipelineState(std::shared_ptr<IPipelineState> pso)          override;
    void bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> rs) override;
    void bindVertexArray(IVertexArray* vao)                              override;
    void draw(uint32_t count)        override;
    void drawIndexed(uint32_t count, IndexType indexType = IndexType::UInt16) override;
    void pipelineBarrier(BarrierType type) override;
    void dispatchCompute(uint32_t nx, uint32_t ny, uint32_t nz) override;

    /// Commit + wait for GPU completion
    void commit();

private:
    MetalRenderDevice*             m_rhiDevice = nullptr;
    MTLCommandBufferRef            m_cmdBuf    = nullptr;
    MTLRenderCommandEncoderRef     m_renderEnc = nullptr;
    MTLComputeCommandEncoderRef    m_computeEnc= nullptr;
    std::shared_ptr<MetalPipelineState>  m_currentPSO;
    std::shared_ptr<MetalResourceSet>    m_currentRS;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_METAL
