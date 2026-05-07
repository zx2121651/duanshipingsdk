#pragma once
#ifdef __APPLE__
#include "../ITexture.h"
#include <cstdint>

namespace sdk { namespace video { namespace rhi {

class MetalTexture final : public ITexture {
public:
    // mtlTexture: __bridge_retained id<MTLTexture> cast to void*
    MetalTexture(void* mtlTexture, int w, int h, TextureFormat fmt);
    ~MetalTexture() override;

    int width()           const override { return m_width; }
    int height()          const override { return m_height; }
    TextureFormat format()const override { return m_format; }
    uint64_t nativeHandle()const override { return reinterpret_cast<uint64_t>(m_texture); }

    void* getMTLTexture() const { return m_texture; }

private:
    void*         m_texture = nullptr; // retained id<MTLTexture>
    int           m_width   = 0;
    int           m_height  = 0;
    TextureFormat m_format;
};

}}} // namespace sdk::video::rhi
#endif // __APPLE__
