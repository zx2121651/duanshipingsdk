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
    VkFormat depthFmt;
    bool operator==(const RenderPassKey& o) const {
        return colorFmt == o.colorFmt && depthFmt == o.depthFmt;
    }
};
struct RenderPassKeyHash {
    size_t operator()(const RenderPassKey& k) const {
        size_t seed = std::hash<uint32_t>()(k.colorFmt);
        return seed ^ (std::hash<uint32_t>()(k.depthFmt) + 0x9e3779b9u + (seed << 6) + (seed >> 2));
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
    std::shared_ptr<IPipelineState> createComputePipeline(const ComputePipelineStateDesc& desc) override;
    std::shared_ptr<IShaderResourceSet> createShaderResourceSet() override;
    std::shared_ptr<ICommandBuffer> createCommandBuffer() override;
    std::shared_ptr<IShaderProgram> createComputeShaderProgram(const char* computeSrc) override;
    std::shared_ptr<IShaderProgram> createShaderProgram(const char* vert, const char* frag) override;
    std::shared_ptr<IShaderProgram> createGeometryShaderProgram(const char*, const char*, const char*) override;
    std::shared_ptr<IShaderProgram> createTessellationProgram(const char*, const char*, const char*, const char*) override;
    std::shared_ptr<ITexture>       createMSAATexture(const TextureDesc& desc, int samples) override;
    void submit(ICommandBuffer* cmdBuffer) override;
    void waitIdle() override;
    std::shared_ptr<ITexture> createTextureFromHardwareBuffer(const HardwareBufferDesc& desc) override;
    std::shared_ptr<ITexture> wrapExternalTexture(const ExternalTextureDesc& desc) override;
    RHICapabilities getCapabilities() const override;

    // Vulkan-specific
    VulkanContext& context() { return m_ctx; }
    VkDescriptorPool descriptorPool() const { return m_descriptorPool; }
    VkDescriptorSetLayout descriptorSetLayout() const { return m_textureSetLayout; }
    VkPipelineLayout defaultPipelineLayout() const { return m_defaultPipelineLayout; }
    VkRenderPass getOrCreateRenderPass(VkFormat colorFmt, VkFormat depthFmt = VK_FORMAT_UNDEFINED);

    std::shared_ptr<VulkanMemoryAllocator> allocator() { return m_allocator; }

private:
    VulkanRenderDevice() = default;
    bool init();
    bool createDescriptorPool();
    bool createDescriptorSetLayout();
    bool createDefaultPipelineLayout();

    VulkanContext m_ctx;
    std::shared_ptr<VulkanMemoryAllocator> m_allocator;

    VkDescriptorPool      m_descriptorPool       = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureSetLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_defaultPipelineLayout = VK_NULL_HANDLE;

    // RenderPass cache
    std::mutex m_mutex;
    std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKeyHash> m_renderPassCache;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
