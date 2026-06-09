#pragma once
#ifdef HAS_METAL

#include "../../../include/rhi/IPipelineState.h"
#include "../../../include/rhi/IBuffer.h"
#include "../../../include/rhi/ITexture.h"
#include <memory>
#include <vector>

#ifdef __OBJC__
#   import <Metal/Metal.h>
    using MTLRenderPipelineStateRef = id<MTLRenderPipelineState>;
    using MTLSamplerStateRef        = id<MTLSamplerState>;
#else
    using MTLRenderPipelineStateRef = void*;
    using MTLSamplerStateRef        = void*;
#endif

namespace sdk {
namespace video {
namespace rhi {

struct MetalPipelineState : public IPipelineState {
    PipelineStateDesc desc;
    const PipelineStateDesc& getDesc() const override { return desc; }
    // Lazily compiled MTLRenderPipelineState (set by MetalCommandBuffer)
    mutable MTLRenderPipelineStateRef mtlPSO = nullptr;
};

struct MetalResourceSet : public IShaderResourceSet {
    enum class BindingType {
        SampledTexture,
        UniformBuffer,
        StorageBuffer,
        ImageTexture
    };

    struct Binding {
        uint32_t slot;
        BindingType type;
        std::shared_ptr<ITexture> texture;
        std::shared_ptr<IBuffer> buffer;
        TextureAccess access;
        TextureFormat format;
        uint32_t level;
    };

    std::vector<Binding> bindings;

    void bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) override;
    void bindUniformBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) override;
    void bindStorageBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) override;
    void bindImageTexture(uint32_t slot, std::shared_ptr<ITexture> texture, TextureAccess access, TextureFormat format, uint32_t level = 0) override;
    void apply() override;

private:
    void upsertBinding(Binding binding);
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_METAL
