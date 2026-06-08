#ifdef HAS_VULKAN
#include "VulkanCommandBuffer.h"
#include "VulkanRenderDevice.h"
#include "VulkanTexture.h"
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

// ---------------------------------------------------------------------------
VulkanDescriptorSet::VulkanDescriptorSet(
    VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout)
    : m_device(device), m_pool(pool)
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;
    if (vkAllocateDescriptorSets(device, &ai, &m_set) != VK_SUCCESS)
        std::cerr << "VulkanDescriptorSet: allocation failed" << std::endl;
}

VulkanDescriptorSet::~VulkanDescriptorSet() {
    if (m_set != VK_NULL_HANDLE)
        vkFreeDescriptorSets(m_device, m_pool, 1, &m_set);
}

void VulkanDescriptorSet::bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) {
    auto* vkTex = dynamic_cast<VulkanTexture*>(texture.get());
    if (!vkTex) return;

    VkDescriptorImageInfo imgInfo = vkTex->descriptorInfo();
    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_set;
    write.dstBinding      = slot;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

// ---------------------------------------------------------------------------
VulkanCommandBuffer::VulkanCommandBuffer(VulkanRenderDevice* device)
    : m_rhiDevice(device)
{
    VkDevice dev = device->context().device();

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = device->context().commandPool();
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev, &ai, &m_cmd);

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(dev, &fi, nullptr, &m_fence);
}

VulkanCommandBuffer::~VulkanCommandBuffer() {
    VkDevice dev = m_rhiDevice->context().device();
    if (m_fence != VK_NULL_HANDLE) vkDestroyFence(dev, m_fence, nullptr);
    if (m_cmd   != VK_NULL_HANDLE)
        vkFreeCommandBuffers(dev, m_rhiDevice->context().commandPool(), 1, &m_cmd);
}

// ---------------------------------------------------------------------------
void VulkanCommandBuffer::begin() {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetFences(m_rhiDevice->context().device(), 1, &m_fence);
    vkBeginCommandBuffer(m_cmd, &bi);
    m_recording = true;
}

void VulkanCommandBuffer::end() {
    vkEndCommandBuffer(m_cmd);
    m_recording = false;
}

void VulkanCommandBuffer::beginRenderPass(const RenderPassDescriptor& desc) {
    if (desc.colorAttachments.empty()) return;
    auto* vkTex = dynamic_cast<VulkanTexture*>(desc.colorAttachments[0].texture.get());
    if (!vkTex) return;

    // Create a temporary framebuffer for this render pass
    VkRenderPass rp = m_rhiDevice->getOrCreateRenderPass(
        VK_FORMAT_R8G8B8A8_UNORM, desc.hasDepthStencil);

    VkFramebufferCreateInfo fci{};
    fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass      = rp;
    VkImageView views[] = {vkTex->imageView()};
    fci.attachmentCount = 1;
    fci.pAttachments    = views;
    fci.width           = vkTex->getWidth();
    fci.height          = vkTex->getHeight();
    fci.layers          = 1;

    VkFramebuffer fb = VK_NULL_HANDLE;
    vkCreateFramebuffer(m_rhiDevice->context().device(), &fci, nullptr, &fb);
    m_pendingFramebuffers.push_back(fb);  // will be destroyed after flush

    VkClearValue clear{};
    auto& cc = desc.colorAttachments[0].clearColor;
    clear.color = {{cc.r, cc.g, cc.b, cc.a}};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = rp;
    rpbi.framebuffer       = fb;
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = {vkTex->getWidth(), vkTex->getHeight()};
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &clear;

    vkCmdBeginRenderPass(m_cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanCommandBuffer::endRenderPass() {
    vkCmdEndRenderPass(m_cmd);
}

void VulkanCommandBuffer::bindPipelineState(std::shared_ptr<IPipelineState> pso) {
    m_currentPSO = std::dynamic_pointer_cast<VulkanPipelineState>(pso);
}

void VulkanCommandBuffer::bindResourceSet(
    uint32_t /*setIndex*/, std::shared_ptr<IShaderResourceSet> rs)
{
    m_currentRS = std::dynamic_pointer_cast<VulkanDescriptorSet>(rs);
}

void VulkanCommandBuffer::bindVertexArray(IVertexArray* /*vao*/) {
    // Vertex layout is encoded in the pipeline state; no separate bind needed
}

void VulkanCommandBuffer::draw(uint32_t count) {
    vkCmdDraw(m_cmd, count, 1, 0, 0);
}

void VulkanCommandBuffer::setViewport(float x, float y, float width, float height) {
    VkViewport vp{};
    vp.x = x; vp.y = y; vp.width = width; vp.height = height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(m_cmd, 0, 1, &vp);
}

void VulkanCommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    VkRect2D rect{};
    rect.offset = {x, y};
    rect.extent = {width, height};
    vkCmdSetScissor(m_cmd, 0, 1, &rect);
}

void VulkanCommandBuffer::drawIndexed(uint32_t indexCount, IndexType indexType) {
    (void)indexType; // VkIndexType set at vkCmdBindIndexBuffer time; IndexType maps to VK_INDEX_TYPE_UINT16/32
    vkCmdDrawIndexed(m_cmd, indexCount, 1, 0, 0, 0);
}

void VulkanCommandBuffer::pipelineBarrier(BarrierType type) {
    VkPipelineStageFlags srcStage, dstStage;
    if (type == BarrierType::Memory) {
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    } else {
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    VkMemoryBarrier mb{};
    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(m_cmd, srcStage, dstStage, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void VulkanCommandBuffer::dispatchCompute(uint32_t nx, uint32_t ny, uint32_t nz) {
    vkCmdDispatch(m_cmd, nx, ny, nz);
}

void VulkanCommandBuffer::flush(VkQueue queue) {
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &m_cmd;
    vkQueueSubmit(queue, 1, &si, m_fence);
    vkWaitForFences(m_rhiDevice->context().device(), 1, &m_fence, VK_TRUE, UINT64_MAX);

    // Destroy transient per-pass framebuffers after GPU finishes
    VkDevice dev = m_rhiDevice->context().device();
    for (VkFramebuffer fb : m_pendingFramebuffers)
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(dev, fb, nullptr);
    m_pendingFramebuffers.clear();
}

void VulkanCommandBuffer::drawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (m_cmd) {
        vkCmdDraw(m_cmd, vertexCount, instanceCount, firstVertex, firstInstance);
    }
}
void VulkanCommandBuffer::drawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, IndexType indexType, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (m_cmd) {
        vkCmdDrawIndexed(m_cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }
}

#endif // HAS_VULKAN
void VulkanDescriptorSet::bindStorageBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) {
    // Stub for vk storage buffer
}
void VulkanDescriptorSet::bindImageTexture(uint32_t slot, std::shared_ptr<ITexture> texture, TextureAccess access, TextureFormat format, uint32_t level) {
    // Stub for vk image texture
}

} // namespace rhi
} // namespace video
} // namespace sdk
