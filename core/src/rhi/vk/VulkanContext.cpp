/**
 * VulkanContext.cpp
 */
#ifdef HAS_VULKAN

#include "VulkanContext.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <stdexcept>

namespace sdk {
namespace video {
namespace rhi {

// ---------------------------------------------------------------------------
// Debug messenger callback
// ---------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void*)
{
    std::cerr << "VK_VALIDATION: " << pData->pMessage << std::endl;
    return VK_FALSE;
}

// ---------------------------------------------------------------------------
bool VulkanContext::init(bool enableValidation) {
    if (!createInstance(enableValidation)) return false;
    if (!pickPhysicalDevice())             return false;
    if (!createLogicalDevice())            return false;
    if (!createCommandPool())              return false;
    return true;
}

void VulkanContext::destroy() {
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(m_instance, m_debugMessenger, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

VulkanContext::~VulkanContext() { destroy(); }

// ---------------------------------------------------------------------------
bool VulkanContext::createInstance(bool enableValidation) {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "ShortVideoSDK";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "ShortVideoSDK RHI";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;

    std::vector<const char*> extensions;
    std::vector<const char*> layers;

    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount       = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames     = layers.data();

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS) {
        std::cerr << "VulkanContext: vkCreateInstance failed" << std::endl;
        return false;
    }

    if (enableValidation) {
        VkDebugUtilsMessengerCreateInfoEXT dbg{};
        dbg.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        dbg.pfnUserCallback = debugCallback;
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (fn) fn(m_instance, &dbg, nullptr, &m_debugMessenger);
    }
    return true;
}

// ---------------------------------------------------------------------------
bool VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        std::cerr << "VulkanContext: no Vulkan-capable GPU found" << std::endl;
        return false;
    }
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devs.data());

    // Prefer discrete GPU; fall back to integrated
    for (auto& d : devs) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            findQueueFamilies(d).isComplete() && checkDeviceExtensions(d)) {
            m_physDevice = d;
            return true;
        }
    }
    // Fallback: first suitable device
    for (auto& d : devs) {
        if (findQueueFamilies(d).isComplete() && checkDeviceExtensions(d)) {
            m_physDevice = d;
            return true;
        }
    }
    std::cerr << "VulkanContext: no suitable GPU found" << std::endl;
    return false;
}

// ---------------------------------------------------------------------------
QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice dev) const {
    QueueFamilyIndices idx;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            idx.graphics = static_cast<int32_t>(i);
            idx.present  = idx.graphics;
            break;
        }
    }
    return idx;
}

bool VulkanContext::checkDeviceExtensions(VkPhysicalDevice dev) const {
    // We only require swapchain for a render-only backend (no presentation needed for offscreen)
    // so no mandatory extensions beyond what's in Vulkan 1.0 core.
    (void)dev;
    return true;
}

// ---------------------------------------------------------------------------
bool VulkanContext::createLogicalDevice() {
    m_queueFamilies = findQueueFamilies(m_physDevice);
    float priority  = 1.0f;

    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = static_cast<uint32_t>(m_queueFamilies.graphics);
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features{};
    features.geometryShader   = VK_TRUE;
    features.tessellationShader = VK_TRUE;
    features.samplerAnisotropy  = VK_TRUE;

    VkDeviceCreateInfo dci{};
    dci.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &qci;
    dci.pEnabledFeatures     = &features;

    if (vkCreateDevice(m_physDevice, &dci, nullptr, &m_device) != VK_SUCCESS) {
        std::cerr << "VulkanContext: vkCreateDevice failed" << std::endl;
        return false;
    }
    vkGetDeviceQueue(m_device, static_cast<uint32_t>(m_queueFamilies.graphics), 0, &m_graphicsQueue);
    return true;
}

// ---------------------------------------------------------------------------
bool VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = static_cast<uint32_t>(m_queueFamilies.graphics);
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool) != VK_SUCCESS) {
        std::cerr << "VulkanContext: vkCreateCommandPool failed" << std::endl;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
VkCommandBuffer VulkanContext::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

// ---------------------------------------------------------------------------
uint32_t VulkanContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    std::cerr << "VulkanContext: failed to find suitable memory type" << std::endl;
    return 0;
}

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
