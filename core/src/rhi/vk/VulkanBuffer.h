#pragma once
#ifdef HAS_VULKAN

#include "../../../include/rhi/IBuffer.h"
#include "VulkanMemoryAllocator.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace sdk {
namespace video {
namespace rhi {

class VulkanBuffer : public IBuffer {
public:
    VulkanBuffer(VkDevice device,
                 std::shared_ptr<VulkanMemoryAllocator> allocator,
                 BufferType type, BufferUsage usage,
                 size_t size, const void* data);
    ~VulkanBuffer() override;

    // IBuffer
    void*  map() override;
    void   unmap() override;
    size_t getSize() const override { return m_size; }
    void   upload(const void* data, size_t size, size_t offset = 0) override;

    VkBuffer handle() const { return m_buffer; }

private:
    VkDevice m_device  = VK_NULL_HANDLE;
    VkBuffer m_buffer  = VK_NULL_HANDLE;
    std::shared_ptr<VulkanMemoryAllocator> m_allocator;
    VulkanMemoryAllocator::Allocation      m_alloc{};
    size_t   m_size     = 0;
    bool     m_hostVisible = false;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
