#ifdef HAS_METAL
#import <Metal/Metal.h>
#include "MetalTexture.h"

namespace sdk {
namespace video {
namespace rhi {

static MTLPixelFormat toMTLFormat(TextureFormat f) {
    switch (f) {
        case TextureFormat::RGBA8:   return MTLPixelFormatRGBA8Unorm;
        case TextureFormat::RGBA16F: return MTLPixelFormatRGBA16Float;
        case TextureFormat::RG16F:   return MTLPixelFormatRG16Float;
        case TextureFormat::Depth24: return MTLPixelFormatDepth32Float;
        case TextureFormat::RGB8:    return MTLPixelFormatRGBA8Unorm; // Metal has no RGB8
        default:                     return MTLPixelFormatRGBA8Unorm;
    }
}

MetalTexture::MetalTexture(MTLDeviceRef device, const TextureDesc& desc, int samples)
    : m_width(desc.width), m_height(desc.height)
{
    MTLTextureDescriptor* td = [MTLTextureDescriptor new];
    td.textureType = (samples > 1) ? MTLTextureType2DMultisample : MTLTextureType2D;
    td.pixelFormat = toMTLFormat(desc.format);
    td.width       = desc.width;
    td.height      = desc.height;
    td.sampleCount = (samples > 1) ? static_cast<NSUInteger>(samples) : 1;
    td.mipmapLevelCount = 1;

    td.usage = MTLTextureUsageShaderRead;
    if (desc.usageFlags & static_cast<uint32_t>(TextureUsage::RenderTarget))
        td.usage |= MTLTextureUsageRenderTarget;
    if (desc.usageFlags & static_cast<uint32_t>(TextureUsage::Storage))
        td.usage |= MTLTextureUsageShaderWrite;

    td.storageMode = MTLStorageModePrivate;

    m_texture = [device newTextureWithDescriptor:td];
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_METAL
