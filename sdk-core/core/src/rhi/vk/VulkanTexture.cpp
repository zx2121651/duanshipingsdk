#ifdef HAS_VULKAN
#include "VulkanTexture.h"
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

static VkFormat toVkFormat(TextureFormat f) {
    switch (f) {
        case TextureFormat::RGBA8:   return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::RG16F:   return VK_FORMAT_R16G16_SFLOAT;
        case TextureFormat::Depth24: return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::RGB8:    return VK_FORMAT_R8G8B8_UNORM;
        case TextureFormat::NV12:    return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM; // VK_KHR_sampler_ycbcr_conversion
        case TextureFormat::BGRA8:   return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::R8:      return VK_FORMAT_R8_UNORM;
        case TextureFormat::R16F:    return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::R32F:    return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::Depth32F:return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::ASTC_4x4:return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case TextureFormat::ASTC_6x6:return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case TextureFormat::ASTC_8x8:return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        default:                     return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

VulkanTexture::VulkanTexture(VkDevice device,
                             std::shared_ptr<VulkanMemoryAllocator> allocator,
                             const TextureDesc& desc,
                             int samples)
    : m_device(device), m_allocator(std::move(allocator)),
      m_width(desc.width), m_height(desc.height), m_format(desc.format), m_samples(samples)
{
    VkFormat fmt = toVkFormat(desc.format);
    bool isDepth = (desc.format == TextureFormat::Depth24 || desc.format == TextureFormat::Depth32F);

    // 1. Create VkImage
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = fmt;
    ici.extent        = {desc.width, desc.height, 1};
    ici.mipLevels     = desc.mipLevels > 0 ? desc.mipLevels : 1;
    ici.arrayLayers   = 1;
    ici.samples       = (samples > 1) ? static_cast<VkSampleCountFlagBits>(samples) : VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc.usageFlags & static_cast<uint32_t>(TextureUsage::RenderTarget))
        ici.usage |= isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                             : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.usageFlags & static_cast<uint32_t>(TextureUsage::Storage))
        ici.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

    if (vkCreateImage(m_device, &ici, nullptr, &m_image) != VK_SUCCESS) {
        std::cerr << "VulkanTexture: vkCreateImage failed" << std::endl;
        return;
    }

    m_alloc = m_allocator->allocateForImage(m_image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 2. ImageView
    VkImageViewCreateInfo vci{};
    vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image    = m_image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format   = fmt;
    vci.subresourceRange.aspectMask     = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(m_device, &vci, nullptr, &m_imageView) != VK_SUCCESS) {
        std::cerr << "VulkanTexture: vkCreateImageView failed" << std::endl;
        return;
    }

    // 3. Sampler
    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    vkCreateSampler(m_device, &sci, nullptr, &m_sampler);
}

VulkanTexture::~VulkanTexture() {
    if (m_device != VK_NULL_HANDLE) {
        if (m_sampler   != VK_NULL_HANDLE) vkDestroySampler(m_device, m_sampler, nullptr);
        if (m_imageView != VK_NULL_HANDLE) vkDestroyImageView(m_device, m_imageView, nullptr);
        if (m_image     != VK_NULL_HANDLE) vkDestroyImage(m_device, m_image, nullptr);
    }
    if (m_allocator) {
        m_allocator->free(m_alloc);
    }
}

VkDescriptorImageInfo VulkanTexture::descriptorInfo() const {
    VkDescriptorImageInfo info{};
    info.sampler     = m_sampler;
    info.imageView   = m_imageView;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return info;
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_VULKAN
