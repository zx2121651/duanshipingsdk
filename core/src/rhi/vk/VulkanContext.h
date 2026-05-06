#pragma once
/**
 * VulkanContext.h
 *
 * Vulkan 核心上下文：VkInstance / VkPhysicalDevice / VkDevice / Queue。
 * 所有 Vulkan 对象通过此类统一创建和销毁。
 *
 * 编译条件：HAS_VULKAN
 */
#ifdef HAS_VULKAN

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>

namespace sdk {
namespace video {
namespace rhi {

struct QueueFamilyIndices {
    int32_t graphics = -1;  // supports graphics + compute
    int32_t present  = -1;  // supports presentation (may equal graphics)

    bool isComplete() const { return graphics >= 0; }
};

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    /// 初始化完整 Vulkan 上下文（Instance → PhysDevice → Device → Queue）
    bool init(bool enableValidation = false);
    void destroy();

    // ---- 访问器 ----
    VkInstance         instance()       const { return m_instance; }
    VkPhysicalDevice   physDevice()     const { return m_physDevice; }
    VkDevice           device()         const { return m_device; }
    VkQueue            graphicsQueue()  const { return m_graphicsQueue; }
    uint32_t           graphicsFamily() const { return static_cast<uint32_t>(m_queueFamilies.graphics); }

    VkCommandPool      commandPool()    const { return m_commandPool; }

    /// 便捷：分配一次性命令缓冲区
    VkCommandBuffer beginSingleTimeCommands();
    void            endSingleTimeCommands(VkCommandBuffer cmd);

    /// 查询物理设备内存类型
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    bool isValid() const { return m_device != VK_NULL_HANDLE; }

private:
    bool createInstance(bool enableValidation);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createCommandPool();
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice dev) const;
    bool checkDeviceExtensions(VkPhysicalDevice dev) const;

    VkInstance         m_instance      = VK_NULL_HANDLE;
    VkPhysicalDevice   m_physDevice    = VK_NULL_HANDLE;
    VkDevice           m_device        = VK_NULL_HANDLE;
    VkQueue            m_graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool      m_commandPool   = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilies;

    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
