#pragma once
#ifdef HAS_METAL

#include "../../../include/rhi/IPipelineState.h"
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
    struct Binding {
        uint32_t                    slot;
        std::shared_ptr<ITexture>   texture;
    };
    std::vector<Binding> bindings;

    void bindTexture(uint32_t slot, std::shared_ptr<ITexture> tex) override {
        for (auto& b : bindings) { if (b.slot == slot) { b.texture = tex; return; } }
        bindings.push_back({slot, tex});
    }
    void apply() override {}
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_METAL
