#pragma once
#ifdef HAS_VULKAN

#include <vulkan/vulkan.h>
#include <cstdint>

namespace sdk {
namespace video {
namespace rhi {

/**
 * VulkanMemoryAllocator — 极简线性显存分配器
 *
 * 两个独立内存池：
 *   - host-visible  (CPU 可写，用于 staging buffer)
 *   - device-local  (GPU 专属，纹理/顶点缓冲区)
 *
 * 分配策略：调用 vkAllocateMemory per-buffer（简单可靠，后续可替换为 VMA）。
 */
class VulkanMemoryAllocator {
public:
    explicit VulkanMemoryAllocator(VkDevice device, VkPhysicalDevice physDevice)
        : m_device(device), m_physDevice(physDevice) {}

    struct Allocation {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize   offset = 0;
        VkDeviceSize   size   = 0;
    };

    /// 分配并绑定 Buffer 内存
    Allocation allocateForBuffer(VkBuffer buffer, VkMemoryPropertyFlags props);

    /// 分配并绑定 Image 内存
    Allocation allocateForImage(VkImage image, VkMemoryPropertyFlags props);

    /// 释放单次分配
    void free(Allocation& alloc);

private:
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    VkDevice         m_device    = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDevice = VK_NULL_HANDLE;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
