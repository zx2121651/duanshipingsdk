#pragma once
#include <memory>
#include <cstdint>
#include "ITexture.h"
#include "IBuffer.h"
#include "IVertexArray.h"
#include "ICommandBuffer.h"
#include "IPipelineState.h"

namespace sdk {
namespace video {
namespace rhi {

// Abstract Factory and Device context for the Rendering Hardware Interface
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    virtual std::shared_ptr<ITexture> createTexture(uint32_t width, uint32_t height) = 0;
    virtual std::shared_ptr<IBuffer> createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data = nullptr) = 0;
    virtual std::shared_ptr<IVertexArray> createVertexArray() = 0;

    // New Pipeline methods
    virtual std::shared_ptr<IPipelineState> createGraphicsPipeline(const GraphicsPipelineDescriptor& desc) = 0;
    virtual std::shared_ptr<IShaderResourceSet> createShaderResourceSet() = 0;

    virtual std::shared_ptr<ICommandBuffer> createCommandBuffer() = 0;
    virtual void submit(ICommandBuffer* cmdBuffer) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
