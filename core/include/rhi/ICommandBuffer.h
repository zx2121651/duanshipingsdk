#include "IVertexArray.h"
#pragma once
#include <cstdint>

namespace sdk {
namespace video {
namespace rhi {

class ITexture;

// Abstract interface for recording rendering commands.
// Future implementations (Vulkan, Metal) will implement this to record into their native command buffers.
class ICommandBuffer {
public:
    virtual ~ICommandBuffer() = default;

    // Begin rendering to the specified output texture
    virtual void beginRenderPass(ITexture* outputTexture) = 0;

    // End the current render pass
    virtual void endRenderPass() = 0;

    // Bind an input texture to a specific slot
    virtual void bindTexture(int slot, ITexture* texture) = 0;
    // Draw using VAO abstraction
    virtual void draw(std::shared_ptr<IVertexArray> vao, int vertexCount) = 0;
    virtual void drawIndexed(std::shared_ptr<IVertexArray> vao, int indexCount) = 0;
    virtual void bindUniformBuffer(uint32_t bindingPoint, std::shared_ptr<IBuffer> ubo) = 0;


    // Dispatch a draw call
};

} // namespace rhi
} // namespace video
} // namespace sdk
