#pragma once
#include <cstdint>

namespace sdk {
namespace video {
namespace rhi {

enum class TextureUsage {
    Sampled = 1 << 0,
    RenderTarget = 1 << 1,
    Storage = 1 << 2
};

struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t usageFlags = static_cast<uint32_t>(TextureUsage::Sampled);
};

class [[nodiscard]] ITexture {
public:
    virtual ~ITexture() = default;

    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;
    virtual uint32_t getId() const = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
