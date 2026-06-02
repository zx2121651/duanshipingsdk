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
    RGBA8,       // GL_RGBA / GL_UNSIGNED_BYTE  (default)
    RGBA16F,     // GL_RGBA / GL_HALF_FLOAT     (HDR rendering)
    RG16F,       // GL_RG   / GL_HALF_FLOAT     (normal maps, motion vectors)
    Depth24,     // GL_DEPTH_COMPONENT / GL_UNSIGNED_INT (depth attachment)
    RGB8,        // GL_RGB  / GL_UNSIGNED_BYTE  (no alpha, legacy compat)
    NV12,        // Y plane + interleaved UV, Android camera/video decode
    BGRA8,       // iOS camera frame default / Metal preferred
    R8,          // Single-channel mask / grayscale (TFLite input)
    R16F,        // Single-channel half-float (depth, distance field)
    R32F,        // Single-channel float (HDR luminance, z-buffer raw)
    Depth32F,    // GL_DEPTH_COMPONENT / GL_FLOAT
    ASTC_4x4,    // Compressed texture for mobile GPU memory saving
    ASTC_6x6,
    ASTC_8x8
};

struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    uint32_t usageFlags = static_cast<uint32_t>(TextureUsage::Sampled);
    uint32_t mipLevels = 1;  ///< 0 = full mip chain, 1 = no mipmaps
};

class [[nodiscard]] ITexture {
public:
    virtual ~ITexture() = default;

    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;
    virtual uint32_t getId() const = 0;
    virtual uint32_t getTarget() const = 0;
    virtual TextureFormat getFormat() const = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
