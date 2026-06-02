#ifdef HAS_VULKAN
#include "VulkanMemoryAllocator.h"
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

uint32_t VulkanMemoryAllocator::findMemoryType(
    uint32_t typeBits, VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    std::cerr << "VulkanMemoryAllocator: no suitable memory type" << std::endl;
    return 0;
}

VulkanMemoryAllocator::Allocation VulkanMemoryAllocator::allocateForBuffer(
    VkBuffer buffer, VkMemoryPropertyFlags props)
{
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(m_device, buffer, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);

    Allocation alloc{};
    alloc.size = req.size;
    if (vkAllocateMemory(m_device, &ai, nullptr, &alloc.memory) != VK_SUCCESS) {
        std::cerr << "VulkanMemoryAllocator: vkAllocateMemory (buffer) failed" << std::endl;
        return {};
    }
    vkBindBufferMemory(m_device, buffer, alloc.memory, 0);
    return alloc;
}

VulkanMemoryAllocator::Allocation VulkanMemoryAllocator::allocateForImage(
    VkImage image, VkMemoryPropertyFlags props)
{
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(m_device, image, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);

    Allocation alloc{};
    alloc.size = req.size;
    if (vkAllocateMemory(m_device, &ai, nullptr, &alloc.memory) != VK_SUCCESS) {
        std::cerr << "VulkanMemoryAllocator: vkAllocateMemory (image) failed" << std::endl;
        return {};
    }
    vkBindImageMemory(m_device, image, alloc.memory, 0);
    return alloc;
}

void VulkanMemoryAllocator::free(Allocation& alloc) {
    if (alloc.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, alloc.memory, nullptr);
        alloc.memory = VK_NULL_HANDLE;
    }
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_VULKAN
