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

    // Compute Shader Support
    // Bind a texture as an image for compute shaders (load/store)
    enum class ImageAccess { ReadOnly, WriteOnly, ReadWrite };
    virtual void bindImageTexture(int unit, ITexture* texture, ImageAccess access) = 0;

    // Execute a compute shader
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
