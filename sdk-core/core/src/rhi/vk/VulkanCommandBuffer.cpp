#ifdef HAS_VULKAN
#include "VulkanCommandBuffer.h"
#include "VulkanRenderDevice.h"
#include "VulkanBuffer.h"
#include "VulkanShaderProgram.h"
#include "VulkanTexture.h"
#include "VulkanVertexArray.h"
#include <algorithm>
#include <array>
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

VulkanPipelineState::~VulkanPipelineState() {
    if (device != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
}

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
    if (!texture) {
        removeBinding(slot);
        return;
    }
    if (!vkTex) return;

    VkDescriptorImageInfo imgInfo = vkTex->descriptorInfo();
    writeImageDescriptor(slot, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imgInfo);
    upsertBinding({slot, BindingType::SampledTexture, texture, nullptr, TextureAccess::Read, texture->getFormat(), 0});
}

void VulkanDescriptorSet::upsertBinding(Binding binding) {
    auto it = std::find_if(m_bindings.begin(), m_bindings.end(), [slot = binding.slot](const Binding& b) {
        return b.slot == slot;
    });
    if (it != m_bindings.end()) {
        *it = std::move(binding);
    } else {
        m_bindings.push_back(std::move(binding));
    }
}

void VulkanDescriptorSet::removeBinding(uint32_t slot) {
    auto it = std::find_if(m_bindings.begin(), m_bindings.end(), [slot](const Binding& b) {
        return b.slot == slot;
    });
    if (it != m_bindings.end()) {
        m_bindings.erase(it);
    }
}

void VulkanDescriptorSet::rewriteTextureBinding(const Binding& binding) {
    auto* vkTex = dynamic_cast<VulkanTexture*>(binding.texture.get());
    if (!vkTex) {
        return;
    }

    if (binding.type == BindingType::SampledTexture) {
        VkDescriptorImageInfo imageInfo = vkTex->descriptorInfo();
        writeImageDescriptor(binding.slot, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageInfo);
    } else if (binding.type == BindingType::ImageTexture) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = VK_NULL_HANDLE;
        imageInfo.imageView = vkTex->imageView();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        writeImageDescriptor(binding.slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, imageInfo);
    }
}

void VulkanDescriptorSet::refreshTextureDescriptors() {
    for (const auto& binding : m_bindings) {
        if (binding.type == BindingType::SampledTexture ||
            binding.type == BindingType::ImageTexture) {
            rewriteTextureBinding(binding);
        }
    }
}

void VulkanDescriptorSet::writeImageDescriptor(uint32_t slot, VkDescriptorType type, const VkDescriptorImageInfo& imageInfo) {
    const uint32_t binding = physicalBinding(slot, type);
    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_set;
    write.dstBinding      = binding;
    write.descriptorType  = type;
    write.descriptorCount = 1;
    write.pImageInfo      = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanDescriptorSet::writeBufferDescriptor(uint32_t slot, VkDescriptorType type, const VkDescriptorBufferInfo& bufferInfo) {
    const uint32_t binding = physicalBinding(slot, type);
    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_set;
    write.dstBinding      = binding;
    write.descriptorType  = type;
    write.descriptorCount = 1;
    write.pBufferInfo     = &bufferInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

uint32_t VulkanDescriptorSet::physicalBinding(uint32_t slot, VkDescriptorType type) const {
    const uint32_t clampedSlot = slot < 16 ? slot : 15;
    switch (type) {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return clampedSlot;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return 16 + clampedSlot;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return 32 + clampedSlot;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return 48 + clampedSlot;
        default:
            return clampedSlot;
    }
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
    if (!m_recording) {
        return;
    }
    if (m_renderPassActive) {
        endRenderPass();
    }
    vkEndCommandBuffer(m_cmd);
    m_recording = false;
}

void VulkanCommandBuffer::beginRenderPass(const RenderPassDescriptor& desc) {
    if (!m_recording) {
        begin();
    }
    if (desc.colorAttachments.empty() || !desc.colorAttachments[0].texture) return;
    auto* vkTex = dynamic_cast<VulkanTexture*>(desc.colorAttachments[0].texture.get());
    if (!vkTex) return;
    VulkanTexture* depthTex = nullptr;
    if (desc.hasDepthStencil) {
        depthTex = dynamic_cast<VulkanTexture*>(desc.depthStencilAttachment.texture.get());
        if (!depthTex || !depthTex->isDepthFormat()) {
            return;
        }
    }

    // Create a temporary framebuffer for this render pass
    VkRenderPass rp = m_rhiDevice->getOrCreateRenderPass(
        vkTex->vkFormat(),
        depthTex ? depthTex->vkFormat() : VK_FORMAT_UNDEFINED);
    transitionTexture(vkTex,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (depthTex) {
        transitionTexture(depthTex,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    }
    m_currentColorAttachment = vkTex;
    m_currentDepthAttachment = depthTex;
    m_currentRenderPass = rp;
    m_currentRenderExtent = {vkTex->getWidth(), vkTex->getHeight()};

    VkFramebufferCreateInfo fci{};
    fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass      = rp;
    std::array<VkImageView, 2> views = {vkTex->imageView(), depthTex ? depthTex->imageView() : VK_NULL_HANDLE};
    fci.attachmentCount = depthTex ? 2u : 1u;
    fci.pAttachments    = views.data();
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
    m_renderPassActive = true;
}

void VulkanCommandBuffer::endRenderPass() {
    if (!m_renderPassActive) {
        return;
    }
    vkCmdEndRenderPass(m_cmd);
    transitionTexture(m_currentColorAttachment,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    m_renderPassActive = false;
    m_currentRenderPass = VK_NULL_HANDLE;
    m_currentRenderExtent = {0, 0};
    m_currentColorAttachment = nullptr;
    m_currentDepthAttachment = nullptr;
}

static VkPrimitiveTopology toVkTopology(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case PrimitiveTopology::TriangleStrip:
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }
}

static VkBlendFactor toVkBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp toVkBlendOp(BlendEquation equation) {
    switch (equation) {
        case BlendEquation::Subtract: return VK_BLEND_OP_SUBTRACT;
        case BlendEquation::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendEquation::Min: return VK_BLEND_OP_MIN;
        case BlendEquation::Max: return VK_BLEND_OP_MAX;
        case BlendEquation::Add:
        default: return VK_BLEND_OP_ADD;
    }
}

static VkCompareOp toVkCompareOp(CompareFunc func) {
    switch (func) {
        case CompareFunc::Never: return VK_COMPARE_OP_NEVER;
        case CompareFunc::Less: return VK_COMPARE_OP_LESS;
        case CompareFunc::Equal: return VK_COMPARE_OP_EQUAL;
        case CompareFunc::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareFunc::Greater: return VK_COMPARE_OP_GREATER;
        case CompareFunc::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case CompareFunc::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareFunc::Always:
        default: return VK_COMPARE_OP_ALWAYS;
    }
}

static VkColorComponentFlags toVkColorWriteMask(ColorWriteMask mask) {
    VkColorComponentFlags flags = 0;
    if ((mask & 0x1) != 0) flags |= VK_COLOR_COMPONENT_R_BIT;
    if ((mask & 0x2) != 0) flags |= VK_COLOR_COMPONENT_G_BIT;
    if ((mask & 0x4) != 0) flags |= VK_COLOR_COMPONENT_B_BIT;
    if ((mask & 0x8) != 0) flags |= VK_COLOR_COMPONENT_A_BIT;
    return flags;
}

static VkFormat toVkVertexFormat(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float1: return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::Byte4: return VK_FORMAT_R8G8B8A8_SINT;
        case VertexFormat::UByte4_Normalized: return VK_FORMAT_R8G8B8A8_UNORM;
        default: return VK_FORMAT_R32_SFLOAT;
    }
}

static size_t hashCombine(size_t seed, size_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

static size_t computeVertexLayoutHash(const VulkanVertexArray* vao) {
    if (!vao) {
        return 0;
    }

    size_t seed = vao->vertexBuffers().size();
    const auto& attributesByBuffer = vao->vertexAttributes();
    for (size_t binding = 0; binding < attributesByBuffer.size(); ++binding) {
        seed = hashCombine(seed, binding);
        for (const auto& attr : attributesByBuffer[binding]) {
            seed = hashCombine(seed, attr.location);
            seed = hashCombine(seed, static_cast<size_t>(attr.format));
            seed = hashCombine(seed, attr.offset);
            seed = hashCombine(seed, attr.stride);
        }
    }
    return seed;
}

static VkAccessFlags accessMaskForLayout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        default:
            return 0;
    }
}

static VkPipelineStageFlags stageMaskForLayout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        default:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

void VulkanCommandBuffer::transitionTexture(VulkanTexture* texture,
                                            VkImageLayout newLayout,
                                            VkAccessFlags dstAccess,
                                            VkPipelineStageFlags dstStage) {
    if (!texture || !m_cmd || texture->image() == VK_NULL_HANDLE) {
        return;
    }

    VkImageLayout oldLayout = texture->currentLayout();
    if (oldLayout == newLayout) {
        return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = accessMaskForLayout(oldLayout);
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image();
    barrier.subresourceRange.aspectMask = texture->aspectMask();
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(m_cmd,
                         stageMaskForLayout(oldLayout),
                         dstStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);
    texture->setCurrentLayout(newLayout);
}

void VulkanCommandBuffer::transitionBoundResourcesForCompute() {
    if (!m_currentRS) {
        return;
    }
    for (const auto& binding : m_currentRS->bindings()) {
        auto* texture = dynamic_cast<VulkanTexture*>(binding.texture.get());
        if (!texture) {
            continue;
        }
        if (binding.type == VulkanDescriptorSet::BindingType::SampledTexture) {
            transitionTexture(texture,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_ACCESS_SHADER_READ_BIT,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        } else if (binding.type == VulkanDescriptorSet::BindingType::ImageTexture) {
            VkAccessFlags access = VK_ACCESS_SHADER_READ_BIT;
            if (binding.access == TextureAccess::Write || binding.access == TextureAccess::ReadWrite) {
                access |= VK_ACCESS_SHADER_WRITE_BIT;
            }
            transitionTexture(texture,
                              VK_IMAGE_LAYOUT_GENERAL,
                              access,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
    }
    m_currentRS->refreshTextureDescriptors();
    rebindCurrentResourceSet();
}

void VulkanCommandBuffer::transitionBoundResourcesForGraphics() {
    if (!m_currentRS) {
        return;
    }
    for (const auto& binding : m_currentRS->bindings()) {
        auto* texture = dynamic_cast<VulkanTexture*>(binding.texture.get());
        if (!texture || binding.type != VulkanDescriptorSet::BindingType::SampledTexture) {
            continue;
        }
        transitionTexture(texture,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    m_currentRS->refreshTextureDescriptors();
    rebindCurrentResourceSet();
}

void VulkanCommandBuffer::rebindCurrentResourceSet() {
    if (!m_currentRS || !m_cmd || !m_rhiDevice || m_rhiDevice->defaultPipelineLayout() == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorSet set = m_currentRS->handle();
    if (set == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindDescriptorSets(m_cmd,
                            m_currentBindPoint,
                            m_rhiDevice->defaultPipelineLayout(),
                            0,
                            1,
                            &set,
                            0,
                            nullptr);
}

bool VulkanCommandBuffer::ensureGraphicsPipeline() {
    if (!m_currentPSO || m_currentPSO->isCompute || !m_currentPSO->desc.shaderProgram || !m_rhiDevice) {
        return false;
    }
    if (m_currentRenderPass == VK_NULL_HANDLE) {
        return false;
    }
    if (m_currentPSO->pipeline != VK_NULL_HANDLE &&
        m_currentPSO->renderPass == m_currentRenderPass &&
        m_currentPSO->extent.width == m_currentRenderExtent.width &&
        m_currentPSO->extent.height == m_currentRenderExtent.height &&
        m_currentPSO->vertexLayoutHash == computeVertexLayoutHash(m_currentVAO)) {
        return true;
    }

    auto* shader = dynamic_cast<VulkanShaderProgram*>(m_currentPSO->desc.shaderProgram);
    if (!shader || shader->getModules().empty()) {
        return false;
    }

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    stages.reserve(shader->getModules().size());
    for (const auto& module : shader->getModules()) {
        if (module.stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            continue;
        }
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = module.stage;
        stage.module = module.module;
        stage.pName = "main";
        stages.push_back(stage);
    }
    if (stages.empty()) {
        return false;
    }

    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    if (m_currentVAO) {
        const auto& attributesByBuffer = m_currentVAO->vertexAttributes();
        vertexBindings.reserve(attributesByBuffer.size());
        for (uint32_t binding = 0; binding < attributesByBuffer.size(); ++binding) {
            const auto& attrs = attributesByBuffer[binding];
            if (attrs.empty()) {
                continue;
            }

            VkVertexInputBindingDescription bindingDesc{};
            bindingDesc.binding = binding;
            bindingDesc.stride = attrs.front().stride;
            bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            vertexBindings.push_back(bindingDesc);

            for (const auto& attr : attrs) {
                VkVertexInputAttributeDescription attrDesc{};
                attrDesc.location = attr.location;
                attrDesc.binding = binding;
                attrDesc.format = toVkVertexFormat(attr.format);
                attrDesc.offset = attr.offset;
                vertexAttributes.push_back(attrDesc);
            }
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
    vertexInput.pVertexBindingDescriptions = vertexBindings.empty() ? nullptr : vertexBindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.empty() ? nullptr : vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVkTopology(m_currentPSO->desc.primitiveTopology);
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_currentRenderExtent.width);
    viewport.height = static_cast<float>(m_currentRenderExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_currentRenderExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr;
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = m_currentPSO->desc.rasterizerState.cullFaceEnabled ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    raster.frontFace = m_currentPSO->desc.rasterizerState.frontFaceCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    raster.depthBiasEnable = (m_currentPSO->desc.rasterizerState.depthBiasConstantFactor != 0.0f ||
                              m_currentPSO->desc.rasterizerState.depthBiasSlopeFactor != 0.0f)
                                 ? VK_TRUE
                                 : VK_FALSE;
    raster.depthBiasConstantFactor = m_currentPSO->desc.rasterizerState.depthBiasConstantFactor;
    raster.depthBiasSlopeFactor = m_currentPSO->desc.rasterizerState.depthBiasSlopeFactor;
    raster.depthBiasClamp = m_currentPSO->desc.rasterizerState.depthBiasClamp;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    const auto& blend = m_currentPSO->desc.blendState;
    colorBlendAttachment.blendEnable = blend.blendEnabled ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = toVkBlendFactor(blend.srcColorFactor);
    colorBlendAttachment.dstColorBlendFactor = toVkBlendFactor(blend.dstColorFactor);
    colorBlendAttachment.colorBlendOp = toVkBlendOp(blend.colorBlendEquation);
    colorBlendAttachment.srcAlphaBlendFactor = toVkBlendFactor(blend.srcAlphaFactor);
    colorBlendAttachment.dstAlphaBlendFactor = toVkBlendFactor(blend.dstAlphaFactor);
    colorBlendAttachment.alphaBlendOp = toVkBlendOp(blend.alphaBlendEquation);
    colorBlendAttachment.colorWriteMask = toVkColorWriteMask(blend.colorWriteMask);

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_currentPSO->desc.depthStencilState.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_currentPSO->desc.depthStencilState.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVkCompareOp(m_currentPSO->desc.depthStencilState.depthCompareFunc);
    depthStencil.stencilTestEnable = m_currentPSO->desc.depthStencilState.stencilTestEnabled ? VK_TRUE : VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_rhiDevice->defaultPipelineLayout();
    pipelineInfo.renderPass = m_currentRenderPass;
    pipelineInfo.subpass = 0;

    VkPipeline newPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(m_rhiDevice->context().device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline);
    if (result != VK_SUCCESS || newPipeline == VK_NULL_HANDLE) {
        std::cerr << "VulkanCommandBuffer: vkCreateGraphicsPipelines failed: " << result << std::endl;
        return false;
    }

    if (m_currentPSO->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_rhiDevice->context().device(), m_currentPSO->pipeline, nullptr);
    }
    m_currentPSO->pipeline = newPipeline;
    m_currentPSO->renderPass = m_currentRenderPass;
    m_currentPSO->extent = m_currentRenderExtent;
    m_currentPSO->vertexLayoutHash = computeVertexLayoutHash(m_currentVAO);
    return true;
}

bool VulkanCommandBuffer::ensureComputePipeline() {
    if (!m_currentPSO || !m_currentPSO->isCompute || !m_currentPSO->computeDesc.shaderProgram || !m_rhiDevice) {
        return false;
    }
    if (m_currentPSO->pipeline != VK_NULL_HANDLE) {
        return true;
    }

    auto* shader = dynamic_cast<VulkanShaderProgram*>(m_currentPSO->computeDesc.shaderProgram);
    if (!shader) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.pName = "main";
    for (const auto& module : shader->getModules()) {
        if (module.stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            stage.module = module.module;
            break;
        }
    }
    if (stage.module == VK_NULL_HANDLE || m_rhiDevice->defaultPipelineLayout() == VK_NULL_HANDLE) {
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = m_rhiDevice->defaultPipelineLayout();

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(m_rhiDevice->context().device(),
                                               VK_NULL_HANDLE,
                                               1,
                                               &pipelineInfo,
                                               nullptr,
                                               &pipeline);
    if (result != VK_SUCCESS || pipeline == VK_NULL_HANDLE) {
        std::cerr << "VulkanCommandBuffer: vkCreateComputePipelines failed: " << result << std::endl;
        return false;
    }

    m_currentPSO->pipeline = pipeline;
    return true;
}

void VulkanCommandBuffer::applyDefaultViewportAndScissor() {
    if (!m_cmd || m_currentRenderExtent.width == 0 || m_currentRenderExtent.height == 0) {
        return;
    }

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_currentRenderExtent.width);
    viewport.height = static_cast<float>(m_currentRenderExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_currentRenderExtent;
    vkCmdSetScissor(m_cmd, 0, 1, &scissor);
}

void VulkanCommandBuffer::bindPipelineState(std::shared_ptr<IPipelineState> pso) {
    m_currentPSO = std::dynamic_pointer_cast<VulkanPipelineState>(pso);
    if (!m_currentPSO) {
        return;
    }

    IShaderProgram* shaderProgram = m_currentPSO->isCompute
        ? m_currentPSO->computeDesc.shaderProgram
        : m_currentPSO->desc.shaderProgram;
    if (!shaderProgram) {
        return;
    }

    auto* shader = dynamic_cast<VulkanShaderProgram*>(shaderProgram);
    if (shader && shader->pushConstantSize() > 0) {
        setPushConstants(shader->pushConstantData(), shader->pushConstantSize());
    }
    if (m_currentPSO->isCompute) {
        if (ensureComputePipeline() && m_currentPSO->pipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_currentPSO->pipeline);
            m_currentBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        }
        return;
    }
    if (m_currentRenderPass != VK_NULL_HANDLE &&
        ensureGraphicsPipeline() &&
        m_currentPSO->pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentPSO->pipeline);
        applyDefaultViewportAndScissor();
        m_currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
}

void VulkanCommandBuffer::bindResourceSet(
    uint32_t setIndex, std::shared_ptr<IShaderResourceSet> rs)
{
    m_currentRS = std::dynamic_pointer_cast<VulkanDescriptorSet>(rs);
    if (!m_currentRS || !m_cmd || !m_rhiDevice || setIndex != 0) {
        return;
    }

    VkPipelineLayout layout = m_rhiDevice->defaultPipelineLayout();
    if (layout == VK_NULL_HANDLE) {
        return;
    }

    rebindCurrentResourceSet();
}

void VulkanCommandBuffer::bindVertexArray(IVertexArray* vao) {
    m_currentVAO = dynamic_cast<VulkanVertexArray*>(vao);
    if (!m_currentVAO || !m_cmd) {
        return;
    }

    const auto& vertexBuffers = m_currentVAO->vertexBuffers();
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceSize> offsets;
    buffers.reserve(vertexBuffers.size());
    offsets.reserve(vertexBuffers.size());

    for (const auto& vertexBuffer : vertexBuffers) {
        auto* vkBuffer = dynamic_cast<VulkanBuffer*>(vertexBuffer.get());
        if (vkBuffer) {
            buffers.push_back(vkBuffer->handle());
            offsets.push_back(0);
        }
    }

    if (!buffers.empty()) {
        vkCmdBindVertexBuffers(m_cmd, 0, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
    }

}

void VulkanCommandBuffer::draw(uint32_t count) {
    m_currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    transitionBoundResourcesForGraphics();
    if (ensureGraphicsPipeline() && m_currentPSO->pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentPSO->pipeline);
        applyDefaultViewportAndScissor();
    }
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

void VulkanCommandBuffer::setPushConstants(const void* data, size_t size) {
    if (!m_cmd || !m_rhiDevice || !data || size == 0) {
        return;
    }

    const size_t clampedSize = (size > 128 ? 128 : size) & ~static_cast<size_t>(3);
    if (clampedSize == 0) {
        return;
    }
    VkPipelineLayout layout = m_rhiDevice->defaultPipelineLayout();
    if (layout == VK_NULL_HANDLE) {
        return;
    }

    vkCmdPushConstants(m_cmd,
                       layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       static_cast<uint32_t>(clampedSize),
                       data);
}

static VkIndexType toVkIndexType(IndexType indexType) {
    return indexType == IndexType::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
}

void VulkanCommandBuffer::drawIndexed(uint32_t indexCount, IndexType indexType) {
    if (!m_currentVAO) return;
    m_currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    transitionBoundResourcesForGraphics();
    if (ensureGraphicsPipeline() && m_currentPSO->pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentPSO->pipeline);
        applyDefaultViewportAndScissor();
    }
    auto indexBuffer = m_currentVAO->indexBuffer();
    auto* vkIndexBuffer = dynamic_cast<VulkanBuffer*>(indexBuffer.get());
    if (!vkIndexBuffer) return;
    vkCmdBindIndexBuffer(m_cmd, vkIndexBuffer->handle(), 0, toVkIndexType(indexType));
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
    m_currentBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    transitionBoundResourcesForCompute();
    if (ensureComputePipeline() && m_currentPSO->pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_currentPSO->pipeline);
    } else {
        return;
    }
    if (m_currentRS && m_rhiDevice && m_rhiDevice->defaultPipelineLayout() != VK_NULL_HANDLE) {
        VkDescriptorSet set = m_currentRS->handle();
        if (set != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(m_cmd,
                                    VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_rhiDevice->defaultPipelineLayout(),
                                    0,
                                    1,
                                    &set,
                                    0,
                                    nullptr);
        }
    }
    vkCmdDispatch(m_cmd, nx, ny, nz);
}

void VulkanCommandBuffer::flush(VkQueue queue) {
    if (!m_recording) {
        return;
    }
    end();

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
        m_currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        transitionBoundResourcesForGraphics();
        if (ensureGraphicsPipeline() && m_currentPSO->pipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentPSO->pipeline);
            applyDefaultViewportAndScissor();
        }
        vkCmdDraw(m_cmd, vertexCount, instanceCount, firstVertex, firstInstance);
    }
}
void VulkanCommandBuffer::drawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, IndexType indexType, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (m_cmd && m_currentVAO) {
        m_currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        transitionBoundResourcesForGraphics();
        if (ensureGraphicsPipeline() && m_currentPSO->pipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentPSO->pipeline);
            applyDefaultViewportAndScissor();
        }
        auto indexBuffer = m_currentVAO->indexBuffer();
        auto* vkIndexBuffer = dynamic_cast<VulkanBuffer*>(indexBuffer.get());
        if (!vkIndexBuffer) return;
        vkCmdBindIndexBuffer(m_cmd, vkIndexBuffer->handle(), 0, toVkIndexType(indexType));
        vkCmdDrawIndexed(m_cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }
}

void VulkanDescriptorSet::bindStorageBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) {
    if (!buffer) {
        removeBinding(slot);
        return;
    }
    auto* vkBuffer = dynamic_cast<VulkanBuffer*>(buffer.get());
    if (!vkBuffer) return;

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vkBuffer->handle();
    bufferInfo.offset = 0;
    bufferInfo.range = buffer->getSize();
    writeBufferDescriptor(slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bufferInfo);
    upsertBinding({slot, BindingType::StorageBuffer, nullptr, buffer, TextureAccess::ReadWrite, TextureFormat::RGBA8, 0});
}

void VulkanDescriptorSet::bindImageTexture(uint32_t slot, std::shared_ptr<ITexture> texture, TextureAccess access, TextureFormat format, uint32_t level) {
    if (!texture) {
        removeBinding(slot);
        return;
    }
    auto* vkTex = dynamic_cast<VulkanTexture*>(texture.get());
    if (!vkTex) return;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = VK_NULL_HANDLE;
    imageInfo.imageView = vkTex->imageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    writeImageDescriptor(slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, imageInfo);
    upsertBinding({slot, BindingType::ImageTexture, texture, nullptr, access, format, level});
}

void VulkanDescriptorSet::bindUniformBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) {
    if (!buffer) {
        removeBinding(slot);
        return;
    }
    auto* vkBuffer = dynamic_cast<VulkanBuffer*>(buffer.get());
    if (!vkBuffer) return;

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vkBuffer->handle();
    bufferInfo.offset = 0;
    bufferInfo.range = buffer->getSize();
    writeBufferDescriptor(slot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, bufferInfo);
    upsertBinding({slot, BindingType::UniformBuffer, nullptr, buffer, TextureAccess::Read, TextureFormat::RGBA8, 0});
}

#endif // HAS_VULKAN

} // namespace rhi
} // namespace video
} // namespace sdk
