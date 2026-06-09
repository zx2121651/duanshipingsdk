#pragma once
#ifdef HAS_VULKAN

#include "../../../include/rhi/ITexture.h"
#include "VulkanMemoryAllocator.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace sdk {
namespace video {
namespace rhi {

class VulkanTexture : public ITexture {
public:
    VulkanTexture(VkDevice device,
                  std::shared_ptr<VulkanMemoryAllocator> allocator,
                  const TextureDesc& desc,
                  int samples = 1);
    ~VulkanTexture() override;

    // ITexture
    uint32_t getWidth()  const override { return m_width; }
    uint32_t getHeight() const override { return m_height; }
    uint32_t getId()     const override { return 0; } // N/A for Vulkan
    uint32_t getTarget() const override { return 0; } // N/A for Vulkan
    TextureFormat getFormat() const override { return m_format; }

    VkImage     image()     const { return m_image; }
    VkImageView imageView() const { return m_imageView; }
    VkSampler   sampler()   const { return m_sampler; }
    VkFormat    vkFormat()  const { return m_vkFormat; }
    bool        isDepthFormat() const {
        return m_format == TextureFormat::Depth24 || m_format == TextureFormat::Depth32F;
    }
    VkImageAspectFlags aspectMask() const {
        return isDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }
    VkImageLayout currentLayout() const { return m_currentLayout; }
    void setCurrentLayout(VkImageLayout layout) { m_currentLayout = layout; }

    VkDescriptorImageInfo descriptorInfo() const;

private:
    VkDevice  m_device    = VK_NULL_HANDLE;
    VkImage   m_image     = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler  m_sampler  = VK_NULL_HANDLE;
    std::shared_ptr<VulkanMemoryAllocator> m_allocator;
    VulkanMemoryAllocator::Allocation      m_alloc{};
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    TextureFormat m_format = TextureFormat::RGBA8;
    VkFormat m_vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    int m_samples = 1;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
