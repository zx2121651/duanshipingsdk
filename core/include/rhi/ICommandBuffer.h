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

    // Dispatch a draw call
    virtual void drawPrimitives(int vertexCount) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
