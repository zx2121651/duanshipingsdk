#pragma once
#ifdef HAS_VULKAN

#include "../../../include/rhi/IRenderDevice.h"
#include "VulkanContext.h"
#include "VulkanMemoryAllocator.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace sdk {
namespace video {
namespace rhi {

class VulkanCommandBuffer;

/// Render pass cache key
struct RenderPassKey {
    VkFormat colorFmt;
    bool     hasDepth;
    bool operator==(const RenderPassKey& o) const {
        return colorFmt == o.colorFmt && hasDepth == o.hasDepth;
    }
};
struct RenderPassKeyHash {
    size_t operator()(const RenderPassKey& k) const {
        return std::hash<uint32_t>()(k.colorFmt) ^ (k.hasDepth ? 1 : 0);
    }
};

class VulkanRenderDevice : public IRenderDevice {
public:
    ~VulkanRenderDevice() override;

    /// Factory: returns nullptr if Vulkan unavailable
    static std::shared_ptr<VulkanRenderDevice> tryCreate();

    // IRenderDevice
    std::shared_ptr<ITexture>       createTexture(const TextureDesc& desc) override;
    std::shared_ptr<IBuffer>        createBuffer(BufferType, BufferUsage, size_t, const void*) override;
    std::shared_ptr<IVertexArray>   createVertexArray() override;
    std::shared_ptr<IPipelineState> createGraphicsPipeline(const PipelineStateDesc& desc) override;
    std::shared_ptr<IShaderResourceSet> createShaderResourceSet() override;
    std::shared_ptr<ICommandBuffer> createCommandBuffer() override;
    std::shared_ptr<IShaderProgram> createShaderProgram(const char* vert, const char* frag) override;
    std::shared_ptr<IShaderProgram> createGeometryShaderProgram(const char*, const char*, const char*) override;
    std::shared_ptr<IShaderProgram> createTessellationProgram(const char*, const char*, const char*, const char*) override;
    std::shared_ptr<ITexture>       createMSAATexture(const TextureDesc& desc, int samples) override;
    void submit(ICommandBuffer* cmdBuffer) override;
    std::shared_ptr<ITexture> bindExternalHardwareBuffer(void* nativeBuffer) override;
    RHICapabilities getCapabilities() const override;

    // Vulkan-specific
    VulkanContext& context() { return m_ctx; }
    VkDescriptorPool descriptorPool() const { return m_descriptorPool; }
    VkRenderPass getOrCreateRenderPass(VkFormat colorFmt, bool hasDepth);

    std::shared_ptr<VulkanMemoryAllocator> allocator() { return m_allocator; }

private:
    VulkanRenderDevice() = default;
    bool init();
    bool createDescriptorPool();
    bool createDescriptorSetLayout();

    VulkanContext m_ctx;
    std::shared_ptr<VulkanMemoryAllocator> m_allocator;

    VkDescriptorPool      m_descriptorPool       = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureSetLayout      = VK_NULL_HANDLE;

    // RenderPass cache
    std::mutex m_mutex;
    std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKeyHash> m_renderPassCache;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
