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

enum class TextureFormat {
    RGBA8,      // GL_RGBA / GL_UNSIGNED_BYTE  (default)
    RGBA16F,    // GL_RGBA / GL_HALF_FLOAT     (HDR rendering)
    RG16F,      // GL_RG   / GL_HALF_FLOAT     (normal maps, motion vectors)
    Depth24,    // GL_DEPTH_COMPONENT / GL_UNSIGNED_INT (depth attachment)
    RGB8        // GL_RGB  / GL_UNSIGNED_BYTE  (no alpha, legacy compat)
};

struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8;
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
