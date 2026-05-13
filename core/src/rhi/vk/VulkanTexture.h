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
    TextureFormat getFormat() const override { return m_format; }

    VkImage     image()     const { return m_image; }
    VkImageView imageView() const { return m_imageView; }
    VkSampler   sampler()   const { return m_sampler; }

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
    int m_samples = 1;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
