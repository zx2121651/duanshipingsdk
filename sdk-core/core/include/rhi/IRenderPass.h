#pragma once
#include <memory>
#include <vector>
#include "ITexture.h"

namespace sdk {
namespace video {
namespace rhi {

enum class LoadAction {
    Load,
    Clear,
    DontCare
};

enum class StoreAction {
    Store,
    DontCare
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct RenderPassColorAttachment {
    std::shared_ptr<ITexture> texture;
    LoadAction loadAction = LoadAction::Clear;
    StoreAction storeAction = StoreAction::Store;
    Color clearColor;
};

struct RenderPassDepthStencilAttachment {
    std::shared_ptr<ITexture> texture;
    LoadAction depthLoadAction = LoadAction::Clear;
    StoreAction depthStoreAction = StoreAction::DontCare;
    float clearDepth = 1.0f;

    LoadAction stencilLoadAction = LoadAction::DontCare;
    StoreAction stencilStoreAction = StoreAction::DontCare;
    uint32_t clearStencil = 0;
};

struct RenderPassDescriptor {
    std::vector<RenderPassColorAttachment> colorAttachments;
    RenderPassDepthStencilAttachment depthStencilAttachment;
    bool hasDepthStencil = false;
};

} // namespace rhi
} // namespace video
} // namespace sdk
