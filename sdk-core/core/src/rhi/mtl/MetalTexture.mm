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
        case TextureFormat::Depth32F:return MTLPixelFormatDepth32Float;
        case TextureFormat::RGB8:    return MTLPixelFormatRGBA8Unorm; // Metal has no RGB8
        case TextureFormat::NV12:    return MTLPixelFormatBGRG8;    // Metal Planar pixel format
        case TextureFormat::BGRA8:   return MTLPixelFormatBGRA8Unorm;
        case TextureFormat::R8:      return MTLPixelFormatR8Unorm;
        case TextureFormat::R16F:    return MTLPixelFormatR16Float;
        case TextureFormat::R32F:    return MTLPixelFormatR32Float;
        case TextureFormat::ASTC_4x4:return MTLPixelFormatASTC_4x4_LDR;
        case TextureFormat::ASTC_6x6:return MTLPixelFormatASTC_6x6_LDR;
        case TextureFormat::ASTC_8x8:return MTLPixelFormatASTC_8x8_LDR;
        default:                     return MTLPixelFormatRGBA8Unorm;
    }
}

MetalTexture::MetalTexture(MTLDeviceRef device, const TextureDesc& desc, int samples)
    : m_width(desc.width), m_height(desc.height), m_format(desc.format)
{
    MTLTextureDescriptor* td = [MTLTextureDescriptor new];
    td.textureType = (samples > 1) ? MTLTextureType2DMultisample : MTLTextureType2D;
    td.pixelFormat = toMTLFormat(desc.format);
    td.width       = desc.width;
    td.height      = desc.height;
    td.sampleCount = (samples > 1) ? static_cast<NSUInteger>(samples) : 1;
    td.mipmapLevelCount = desc.mipLevels > 0 ? desc.mipLevels : 1;
    m_mipLevels = td.mipmapLevelCount;

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
