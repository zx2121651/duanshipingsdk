#pragma once
#ifdef HAS_VULKAN

#include "../../../include/rhi/ICommandBuffer.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace sdk {
namespace video {
namespace rhi {

class VulkanRenderDevice;
class VulkanShaderProgram;

struct VulkanPipelineState : public IPipelineState {
    PipelineStateDesc desc;
    const PipelineStateDesc& getDesc() const override { return desc; }
    // Cached VkPipeline handle (lazily created by command buffer at draw time)
    mutable VkPipeline pipeline = VK_NULL_HANDLE;
};

struct VulkanDescriptorSet : public IShaderResourceSet {
    VulkanDescriptorSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout);
    ~VulkanDescriptorSet() override;
    void bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) override;
    void bindUniformBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) override;
    void apply() override {}
    VkDescriptorSet handle() const { return m_set; }

private:
    VkDevice         m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_pool   = VK_NULL_HANDLE;
    VkDescriptorSet  m_set    = VK_NULL_HANDLE;
};

/**
 * VulkanCommandBuffer — records Vulkan GPU commands.
 * flush() submits to the graphics queue and waits (single-frame fence).
 */
class VulkanCommandBuffer : public ICommandBuffer {
public:
    explicit VulkanCommandBuffer(VulkanRenderDevice* device);
    ~VulkanCommandBuffer() override;

    void begin() override;
    void end() override;

    void beginRenderPass(const RenderPassDescriptor& desc) override;
    void endRenderPass() override;

    void setViewport(float x, float y, float width, float height) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;
    void setPushConstants(const void* data, size_t size) override;

    void bindPipelineState(std::shared_ptr<IPipelineState> pso) override;
    void bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> rs) override;
    void bindVertexArray(IVertexArray* vao) override;

    void draw(uint32_t count) override;
    void drawIndexed(uint32_t indexCount, IndexType indexType = IndexType::UInt16) override;

    void pipelineBarrier(BarrierType type) override;
    void dispatchCompute(uint32_t nx, uint32_t ny, uint32_t nz) override;

    /// Submit to queue and wait for completion (blocking, single-frame)
    void flush(VkQueue queue);

private:
    VulkanRenderDevice*                   m_rhiDevice = nullptr;
    VkCommandBuffer                       m_cmd   = VK_NULL_HANDLE;
    VkFence                               m_fence = VK_NULL_HANDLE;
    bool                                  m_recording = false;
    std::shared_ptr<VulkanPipelineState>  m_currentPSO;
    std::shared_ptr<VulkanDescriptorSet>  m_currentRS;
    // Framebuffers created per-renderpass are deferred-destroyed after flush
    std::vector<VkFramebuffer>            m_pendingFramebuffers;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
