#pragma once
#include "../../include/rhi/IRenderDevice.h"
#include "../../include/rhi/ICommandBuffer.h"
#include "GLTexture.h"

namespace sdk {
namespace video {
namespace rhi {

class GLCommandBuffer : public ICommandBuffer {
public:
    GLCommandBuffer() = default;
    ~GLCommandBuffer() override = default;

    void beginRenderPass(ITexture* outputTexture) override;
    void endRenderPass() override;
    void bindTexture(int slot, ITexture* texture) override;
    void drawPrimitives(int vertexCount) override;
};

class GLRenderDevice : public IRenderDevice {
public:
    GLRenderDevice() = default;
    ~GLRenderDevice() override = default;

    std::shared_ptr<ITexture> createTexture(uint32_t width, uint32_t height) override;
    std::shared_ptr<ICommandBuffer> createCommandBuffer() override;
    void submit(ICommandBuffer* cmdBuffer) override;
};

} // namespace rhi
} // namespace video
} // namespace sdk
