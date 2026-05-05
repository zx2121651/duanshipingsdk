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

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    [[nodiscard]] virtual std::shared_ptr<ITexture> createTexture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual std::shared_ptr<IBuffer> createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data = nullptr) = 0;
    [[nodiscard]] virtual std::shared_ptr<IVertexArray> createVertexArray() = 0;

    [[nodiscard]] virtual std::shared_ptr<IPipelineState> createGraphicsPipeline(const PipelineStateDesc& desc) = 0;
    [[nodiscard]] virtual std::shared_ptr<IShaderResourceSet> createShaderResourceSet() = 0;

    [[nodiscard]] virtual std::shared_ptr<ICommandBuffer> createCommandBuffer() = 0;

    virtual void submit(ICommandBuffer* cmdBuffer) = 0;

    // External bindings
    virtual std::shared_ptr<ITexture> bindExternalHardwareBuffer(void* nativeBuffer) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
