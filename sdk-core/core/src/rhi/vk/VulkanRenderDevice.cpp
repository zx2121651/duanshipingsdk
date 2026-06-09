#ifdef HAS_VULKAN
#include "VulkanCommandBuffer.h"  // defines VulkanPipelineState, VulkanDescriptorSet
#include "VulkanRenderDevice.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanVertexArray.h"
#include "VulkanShaderProgram.h"
#include "../../../include/rhi/IVertexArray.h"
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

// ---------------------------------------------------------------------------
std::shared_ptr<VulkanRenderDevice> VulkanRenderDevice::tryCreate() {
    auto dev = std::shared_ptr<VulkanRenderDevice>(new VulkanRenderDevice());
    if (!dev->init()) return nullptr;
    return dev;
}

bool VulkanRenderDevice::init() {
    if (!m_ctx.init(false)) return false;
    m_allocator = std::make_shared<VulkanMemoryAllocator>(m_ctx.device(), m_ctx.physDevice());
    if (!createDescriptorPool()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createDefaultPipelineLayout()) return false;
    std::cout << "VulkanRenderDevice: initialized successfully" << std::endl;
    return true;
}

VulkanRenderDevice::~VulkanRenderDevice() {
    if (m_defaultPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_ctx.device(), m_defaultPipelineLayout, nullptr);
    if (m_textureSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_ctx.device(), m_textureSetLayout, nullptr);
    if (m_descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_ctx.device(), m_descriptorPool, nullptr);
    for (auto& [k, rp] : m_renderPassCache)
        vkDestroyRenderPass(m_ctx.device(), rp, nullptr);
    m_ctx.destroy();
}

// ---------------------------------------------------------------------------
bool VulkanRenderDevice::createDescriptorPool() {
    constexpr uint32_t kMaxDescriptorSets = 512;
    constexpr uint32_t kLogicalSlotsPerType = 16;
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDescriptorSets * kLogicalSlotsPerType},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         kMaxDescriptorSets * kLogicalSlotsPerType},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kMaxDescriptorSets * kLogicalSlotsPerType},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          kMaxDescriptorSets * kLogicalSlotsPerType},
    };
    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = kMaxDescriptorSets;
    ci.poolSizeCount = static_cast<uint32_t>(sizeof(sizes) / sizeof(sizes[0]));
    ci.pPoolSizes    = sizes;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    return vkCreateDescriptorPool(m_ctx.device(), &ci, nullptr, &m_descriptorPool) == VK_SUCCESS;
}

bool VulkanRenderDevice::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(64);

    auto addBinding = [&](uint32_t slot, VkDescriptorType type, VkShaderStageFlags stages) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = slot;
        binding.descriptorType = type;
        binding.descriptorCount = 1;
        binding.stageFlags = stages;
        bindings.push_back(binding);
    };

    for (uint32_t slot = 0; slot < 16; ++slot) {
        addBinding(slot, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
    }
    for (uint32_t slot = 16; slot < 32; ++slot) {
        addBinding(slot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
    }
    for (uint32_t slot = 32; slot < 48; ++slot) {
        addBinding(slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
    }
    for (uint32_t slot = 48; slot < 64; ++slot) {
        addBinding(slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
    }

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();
    return vkCreateDescriptorSetLayout(m_ctx.device(), &ci, nullptr, &m_textureSetLayout) == VK_SUCCESS;
}

bool VulkanRenderDevice::createDefaultPipelineLayout() {
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = 128;

    VkPipelineLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount = 1;
    ci.pSetLayouts = &m_textureSetLayout;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges = &pushRange;

    return vkCreatePipelineLayout(m_ctx.device(), &ci, nullptr, &m_defaultPipelineLayout) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
VkRenderPass VulkanRenderDevice::getOrCreateRenderPass(VkFormat colorFmt, VkFormat depthFmt) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RenderPassKey key{colorFmt, depthFmt};
    auto it = m_renderPassCache.find(key);
    if (it != m_renderPassCache.end()) return it->second;

    VkAttachmentDescription colorAttach{};
    colorAttach.format         = colorFmt;
    colorAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments = {colorAttach};

    VkAttachmentDescription depthAttach{};
    VkAttachmentReference   depthRef{};
    if (depthFmt != VK_FORMAT_UNDEFINED) {
        depthAttach.format         = depthFmt;
        depthAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttach.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttach.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttach.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthRef                   = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        subpass.pDepthStencilAttachment = &depthRef;
        attachments.push_back(depthAttach);
    }

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments    = attachments.data();
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    vkCreateRenderPass(m_ctx.device(), &rpci, nullptr, &rp);
    m_renderPassCache[key] = rp;
    return rp;
}

// ---------------------------------------------------------------------------
std::shared_ptr<ITexture> VulkanRenderDevice::createTexture(const TextureDesc& desc) {
    return std::make_shared<VulkanTexture>(m_ctx.device(), m_allocator, desc, 1);
}

std::shared_ptr<IBuffer> VulkanRenderDevice::createBuffer(
    BufferType type, BufferUsage usage, size_t size, const void* data)
{
    return std::make_shared<VulkanBuffer>(m_ctx.device(), m_allocator, type, usage, size, data);
}

std::shared_ptr<IVertexArray> VulkanRenderDevice::createVertexArray() {
    return std::make_shared<VulkanVertexArray>();
}

std::shared_ptr<IPipelineState> VulkanRenderDevice::createGraphicsPipeline(
    const PipelineStateDesc& desc)
{
    // VulkanCommandBuffer lazily materializes the VkPipeline once a render pass is active.
    auto pso = std::make_shared<VulkanPipelineState>(m_ctx.device());
    pso->desc = desc;
    return pso;
}

std::shared_ptr<IShaderResourceSet> VulkanRenderDevice::createShaderResourceSet() {
    return std::make_shared<VulkanDescriptorSet>(m_ctx.device(), m_descriptorPool, m_textureSetLayout);
}

std::shared_ptr<ICommandBuffer> VulkanRenderDevice::createCommandBuffer() {
    return std::make_shared<VulkanCommandBuffer>(this);
}

std::shared_ptr<IShaderProgram> VulkanRenderDevice::createShaderProgram(
    const char* /*vert*/, const char* /*frag*/)
{
    // GLSL → SPIR-V compilation happens offline via glslangValidator.
    // At runtime, load precompiled SPIR-V (embedded as uint32_t[] in the .cpp).
    // Returning stub here; callers that embed SPIR-V use createFromSPIRV() directly.
    std::cerr << "VulkanRenderDevice: createShaderProgram(GLSL) called — "
                 "use SPIR-V path via VulkanShaderProgram::createFromSPIRV instead" << std::endl;
    return nullptr;
}

std::shared_ptr<IShaderProgram> VulkanRenderDevice::createGeometryShaderProgram(
    const char*, const char*, const char*)
{
    std::cerr << "VulkanRenderDevice: use SPIR-V path for geometry shaders" << std::endl;
    return nullptr;
}

std::shared_ptr<IShaderProgram> VulkanRenderDevice::createTessellationProgram(
    const char*, const char*, const char*, const char*)
{
    std::cerr << "VulkanRenderDevice: use SPIR-V path for tessellation shaders" << std::endl;
    return nullptr;
}

std::shared_ptr<ITexture> VulkanRenderDevice::createMSAATexture(
    const TextureDesc& desc, int samples)
{
    // Clamp samples to power of two between 1 and 16
    int clampedSamples = samples;
    if (clampedSamples <= 1) clampedSamples = 1;
    if (clampedSamples > 16) clampedSamples = 16;
    return std::make_shared<VulkanTexture>(m_ctx.device(), m_allocator, desc, clampedSamples);
}

void VulkanRenderDevice::submit(ICommandBuffer* cmdBuffer) {
    auto* vkCmd = static_cast<VulkanCommandBuffer*>(cmdBuffer);
    if (vkCmd) vkCmd->flush(m_ctx.graphicsQueue());
}

void VulkanRenderDevice::waitIdle() {
    if (m_ctx.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_ctx.device());
    }
}


RHICapabilities VulkanRenderDevice::getCapabilities() const {
    RHICapabilities caps;
    caps.backend         = BackendType::VULKAN;
    caps.computeShader   = true;  // Vulkan 1.0 mandates compute pipeline
    caps.geometryShader  = true;  // Vulkan 1.0 mandatory feature
    caps.tessellation    = true;  // Vulkan 1.0 mandatory feature
    caps.msaa            = true;
    caps.maxMSAASamples  = 8;     // Conservative: all Vulkan 1.0 HW supports 4x+ MSAA
    caps.fp16RenderTarget= true;
    caps.astc            = false; // Device-dependent; conservative default
    caps.glesVersionInt  = 0;     // N/A for Vulkan
    caps.rendererString  = "";
    return caps;
}


std::shared_ptr<ITexture> VulkanRenderDevice::createTextureFromHardwareBuffer(const HardwareBufferDesc& desc) {
    (void)desc;
    // Explicit external memory import is backend/platform specific and must not
    // reinterpret GLES texture ids as Vulkan images.
    return nullptr;
}

std::shared_ptr<ITexture> VulkanRenderDevice::wrapExternalTexture(const ExternalTextureDesc& desc) {
    (void)desc;
    return nullptr;
}

std::shared_ptr<IPipelineState> VulkanRenderDevice::createComputePipeline(const ComputePipelineStateDesc& desc) {
    // VulkanCommandBuffer lazily materializes the VkPipeline at dispatch time.
    auto pso = std::make_shared<VulkanPipelineState>(m_ctx.device());
    pso->computeDesc = desc;
    pso->isCompute = true;
    return pso;
}
std::shared_ptr<IShaderProgram> VulkanRenderDevice::createComputeShaderProgram(const char* computeSrc) {
    (void)computeSrc;
    std::cerr << "VulkanRenderDevice: createComputeShaderProgram(GLSL) called - "
                 "use SPIR-V path via VulkanShaderProgram::createFromSPIRV instead" << std::endl;
    return nullptr;
}

#endif // HAS_VULKAN

} // namespace rhi
} // namespace video
} // namespace sdk
