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
class VulkanTexture;
class VulkanVertexArray;

struct VulkanPipelineState : public IPipelineState {
    explicit VulkanPipelineState(VkDevice device = VK_NULL_HANDLE) : device(device) {}
    ~VulkanPipelineState() override;

    PipelineStateDesc desc;
    ComputePipelineStateDesc computeDesc;
    bool isCompute = false;
    const PipelineStateDesc& getDesc() const override { return desc; }
    // Cached VkPipeline handle (lazily created by command buffer at draw time)
    VkDevice device = VK_NULL_HANDLE;
    mutable VkPipeline pipeline = VK_NULL_HANDLE;
    mutable VkRenderPass renderPass = VK_NULL_HANDLE;
    mutable VkExtent2D extent{0, 0};
    mutable size_t vertexLayoutHash = 0;
};

struct VulkanDescriptorSet : public IShaderResourceSet {
    VulkanDescriptorSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout);
    ~VulkanDescriptorSet() override;
    void bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) override;
    void bindUniformBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) override;
    void bindStorageBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) override;
    void bindImageTexture(uint32_t slot, std::shared_ptr<ITexture> texture, TextureAccess access, TextureFormat format, uint32_t level = 0) override;
    void apply() override {}
    VkDescriptorSet handle() const { return m_set; }
    enum class BindingType {
        SampledTexture,
        UniformBuffer,
        StorageBuffer,
        ImageTexture
    };

    struct Binding {
        uint32_t slot = 0;
        BindingType type = BindingType::SampledTexture;
        std::shared_ptr<ITexture> texture;
        std::shared_ptr<IBuffer> buffer;
        TextureAccess access = TextureAccess::Read;
        TextureFormat format = TextureFormat::RGBA8;
        uint32_t level = 0;
    };
    const std::vector<Binding>& bindings() const { return m_bindings; }
    void refreshTextureDescriptors();

private:
    void upsertBinding(Binding binding);
    void removeBinding(uint32_t slot);
    void rewriteTextureBinding(const Binding& binding);
    uint32_t physicalBinding(uint32_t slot, VkDescriptorType type) const;
    void writeImageDescriptor(uint32_t slot, VkDescriptorType type, const VkDescriptorImageInfo& imageInfo);
    void writeBufferDescriptor(uint32_t slot, VkDescriptorType type, const VkDescriptorBufferInfo& bufferInfo);

    VkDevice         m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_pool   = VK_NULL_HANDLE;
    VkDescriptorSet  m_set    = VK_NULL_HANDLE;
    std::vector<Binding> m_bindings;
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
    void drawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void drawIndexed(uint32_t indexCount, IndexType indexType = IndexType::UInt16) override;
    void drawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, IndexType indexType = IndexType::UInt16, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;

    void pipelineBarrier(BarrierType type) override;
    void dispatchCompute(uint32_t nx, uint32_t ny, uint32_t nz) override;

    /// Submit to queue and wait for completion (blocking, single-frame)
    void flush(VkQueue queue);

private:
    bool ensureGraphicsPipeline();
    bool ensureComputePipeline();
    void applyDefaultViewportAndScissor();
    void transitionTexture(VulkanTexture* texture,
                           VkImageLayout newLayout,
                           VkAccessFlags dstAccess,
                           VkPipelineStageFlags dstStage);
    void transitionBoundResourcesForCompute();
    void transitionBoundResourcesForGraphics();
    void rebindCurrentResourceSet();

    VulkanRenderDevice*                   m_rhiDevice = nullptr;
    VkCommandBuffer                       m_cmd   = VK_NULL_HANDLE;
    VkFence                               m_fence = VK_NULL_HANDLE;
    bool                                  m_recording = false;
    bool                                  m_renderPassActive = false;
    VkRenderPass                          m_currentRenderPass = VK_NULL_HANDLE;
    VkExtent2D                            m_currentRenderExtent{0, 0};
    std::shared_ptr<VulkanPipelineState>  m_currentPSO;
    std::shared_ptr<VulkanDescriptorSet>  m_currentRS;
    VulkanVertexArray*                    m_currentVAO = nullptr;
    VulkanTexture*                        m_currentColorAttachment = nullptr;
    VulkanTexture*                        m_currentDepthAttachment = nullptr;
    VkPipelineBindPoint                   m_currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    // Framebuffers created per-renderpass are deferred-destroyed after flush
    std::vector<VkFramebuffer>            m_pendingFramebuffers;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
