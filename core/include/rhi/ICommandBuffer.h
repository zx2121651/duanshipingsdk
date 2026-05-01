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

class ICommandBuffer {
public:
    virtual ~ICommandBuffer() = default;

    virtual void beginRenderPass(const RenderPassDescriptor& descriptor) = 0;
    virtual void endRenderPass() = 0;

    virtual void bindPipeline(std::shared_ptr<IPipelineState> pipeline) = 0;
    virtual void bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> resourceSet) = 0;

    // We keep bindVertexBuffer/IndexBuffer logic through VAO or similar abstractions, but simplified here:
    virtual void bindVertexArray(std::shared_ptr<IVertexArray> vao) = 0;

    virtual void draw(int vertexCount, int instanceCount = 1) = 0;
    virtual void drawIndexed(int indexCount, int instanceCount = 1) = 0;

    // Compute Shader Support
    virtual void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) = 0;

    // Memory barrier to synchronize compute and graphics operations
    enum class BarrierBit {
        VertexAttribArray = 1 << 0,
        ElementArray = 1 << 1,
        Uniform = 1 << 2,
        TextureFetch = 1 << 3,
        ShaderImageAccess = 1 << 4
    };
    virtual void memoryBarrier(uint32_t barriers) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
