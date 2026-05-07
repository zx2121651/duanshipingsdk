#ifdef __APPLE__
#import <Metal/Metal.h>
#include "../../../include/rhi/metal/MetalTexture.h"

namespace sdk { namespace video { namespace rhi {

MetalTexture::MetalTexture(void* mtlTex, int w, int h, TextureFormat fmt)
    : m_texture(mtlTex), m_width(w), m_height(h), m_format(fmt) {}

MetalTexture::~MetalTexture() {
    if (m_texture) {
        CFRelease(m_texture);
        m_texture = nullptr;
    }
}

}}} // namespace sdk::video::rhi
#endif
