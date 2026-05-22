#pragma once
#ifdef HAS_METAL

#include "../../../include/rhi/ITexture.h"
#ifdef __OBJC__
#   import <Metal/Metal.h>
    using MTLDeviceRef  = id<MTLDevice>;
    using MTLTextureRef = id<MTLTexture>;
#else
    using MTLDeviceRef  = void*;
    using MTLTextureRef = void*;
#endif

namespace sdk {
namespace video {
namespace rhi {

class MetalTexture : public ITexture {
public:
    MetalTexture(MTLDeviceRef device, const TextureDesc& desc, int samples = 1);
    ~MetalTexture() override = default;

    uint32_t getWidth()  const override { return m_width; }
    uint32_t getHeight() const override { return m_height; }
    uint32_t getId()     const override { return 0; } // N/A for Metal
    TextureFormat getFormat() const override { return m_format; }

    MTLTextureRef mtlTexture() const { return m_texture; }

private:
    MTLTextureRef m_texture = nullptr;
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    TextureFormat m_format = TextureFormat::RGBA8;
    uint32_t m_mipLevels = 1;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_METAL
