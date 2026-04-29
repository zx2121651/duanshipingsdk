#include "IBuffer.h"
#include "IVertexArray.h"
#pragma once
#include <memory>
#include <cstdint>
#include "ITexture.h"
#include "ICommandBuffer.h"

namespace sdk {
namespace video {
namespace rhi {

// Abstract Factory and Device context for the Rendering Hardware Interface
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    // Create a new texture managed by the RHI
    virtual std::shared_ptr<ITexture> createTexture(uint32_t width, uint32_t height) = 0;

    // Create a command buffer for recording rendering instructions
    // Buffer Object creation
    virtual std::shared_ptr<IBuffer> createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data = nullptr) = 0;
    virtual std::shared_ptr<IVertexArray> createVertexArray() = 0;

    virtual std::shared_ptr<ICommandBuffer> createCommandBuffer() = 0;

    // Submit a recorded command buffer for execution
    virtual void submit(ICommandBuffer* cmdBuffer) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
