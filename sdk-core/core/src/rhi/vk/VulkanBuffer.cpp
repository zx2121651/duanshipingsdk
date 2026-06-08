#ifdef HAS_VULKAN
#include "VulkanBuffer.h"
#include <cstring>
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

static VkBufferUsageFlags toVkUsage(BufferType type, BufferUsage usage) {
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (type == BufferType::VertexBuffer) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (type == BufferType::IndexBuffer)  flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (type == BufferType::UniformBuffer) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (type == BufferType::StorageBuffer) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (usage == BufferUsage::DynamicDraw) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    return flags;
}

VulkanBuffer::VulkanBuffer(VkDevice device,
                           std::shared_ptr<VulkanMemoryAllocator> allocator,
                           BufferType type, BufferUsage usage,
                           size_t size, const void* data)
    : m_device(device), m_allocator(std::move(allocator)), m_size(size), m_type(type)
{
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = toVkUsage(type, usage);
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &ci, nullptr, &m_buffer) != VK_SUCCESS) {
        std::cerr << "VulkanBuffer: vkCreateBuffer failed" << std::endl;
        return;
    }

    // For DynamicDraw use HOST_VISIBLE; otherwise DEVICE_LOCAL
    VkMemoryPropertyFlags memProps =
        (usage == BufferUsage::DynamicDraw)
            ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    m_hostVisible = (usage == BufferUsage::DynamicDraw);

    m_alloc = m_allocator->allocateForBuffer(m_buffer, memProps);

    if (data && m_hostVisible) {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_alloc.memory, 0, size, 0, &mapped);
        std::memcpy(mapped, data, size);
        vkUnmapMemory(m_device, m_alloc.memory);
    }
}

VulkanBuffer::~VulkanBuffer() {
    if (m_buffer != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_buffer, nullptr);
    }
    if (m_allocator) {
        m_allocator->free(m_alloc);
    }
}

void* VulkanBuffer::map(size_t offset, size_t size, BufferAccess /*access*/) {
    if (!m_hostVisible || m_alloc.memory == VK_NULL_HANDLE) return nullptr;
    void* ptr = nullptr;
    vkMapMemory(m_device, m_alloc.memory, offset, size == 0 ? m_size : size, 0, &ptr);
    return ptr;
}

void VulkanBuffer::unmap() {
    if (m_hostVisible && m_alloc.memory != VK_NULL_HANDLE)
        vkUnmapMemory(m_device, m_alloc.memory);
}

void VulkanBuffer::updateData(const void* data, size_t size, size_t offset) {
    if (!m_hostVisible) return;
    void* mapped = nullptr;
    vkMapMemory(m_device, m_alloc.memory, offset, size, 0, &mapped);
    std::memcpy(mapped, data, size);
    vkUnmapMemory(m_device, m_alloc.memory);
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_VULKAN
