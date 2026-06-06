#pragma once
#include <memory>
#include <cstdint>
#include "IVertexArray.h"
#include "IRenderPass.h"
#include "IPipelineState.h"
#include "IBuffer.h"

namespace sdk {
namespace video {
namespace rhi {

class ITexture;

enum class BarrierType {
    Memory,
    Pipeline
};

enum class IndexType {
    UInt16,
    UInt32
};

class [[nodiscard]] ICommandBuffer {
public:
    virtual ~ICommandBuffer() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void beginRenderPass(const RenderPassDescriptor& desc) = 0;
    virtual void endRenderPass() = 0;

    virtual void setViewport(float x, float y, float width, float height) = 0;
    virtual void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;

    virtual void bindPipelineState(std::shared_ptr<IPipelineState> pso) = 0;
    virtual void bindVertexArray(IVertexArray* vao) = 0;
    virtual void draw(uint32_t count) = 0;
    virtual void drawIndexed(uint32_t indexCount, IndexType indexType = IndexType::UInt16) = 0;

    virtual void pipelineBarrier(BarrierType type) = 0;

    // Set uniform push constants directly without creating a buffer
    virtual void setPushConstants(const void* data, size_t size) = 0;

    // Kept for compute, bindResourceSet can be integrated
    virtual void bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> resourceSet) = 0;
    virtual void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
