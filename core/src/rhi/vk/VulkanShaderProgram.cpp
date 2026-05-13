#ifdef HAS_VULKAN
#include "VulkanShaderProgram.h"
#include <iostream>
#include <cstring>

namespace sdk {
namespace video {
namespace rhi {

std::shared_ptr<VulkanShaderProgram> VulkanShaderProgram::createFromSPIRV(
    VkDevice device, const std::vector<StageSource>& stages)
{
    auto prog = std::shared_ptr<VulkanShaderProgram>(new VulkanShaderProgram());
    prog->m_device  = device;
    prog->m_pcData.resize(128, 0);

    for (const auto& src : stages) {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = src.spirv.size() * sizeof(uint32_t);
        ci.pCode    = src.spirv.data();

        VkShaderModule mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) {
            std::cerr << "VulkanShaderProgram: vkCreateShaderModule failed" << std::endl;
            continue;
        }
        prog->m_modules.push_back({toVkStage(src.stage), mod});
    }
    return prog;
}

VulkanShaderProgram::~VulkanShaderProgram() {
    for (auto& m : m_modules)
        if (m.module != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_device, m.module, nullptr);
}

void VulkanShaderProgram::registerPushConstant(const std::string& name, uint32_t size) {
    if (m_pcOffsets.count(name)) return;
    if (m_pcCursor + size > 128) { std::cerr << "Push constant overflow" << std::endl; return; }
    m_pcOffsets[name] = m_pcCursor;
    m_pcCursor += size;
}

template<typename T>
void VulkanShaderProgram::writePushConstant(const std::string& name, const T& val) {
    registerPushConstant(name, static_cast<uint32_t>(sizeof(T)));
    auto it = m_pcOffsets.find(name);
    if (it != m_pcOffsets.end())
        std::memcpy(m_pcData.data() + it->second, &val, sizeof(T));
}

void VulkanShaderProgram::setUniform1i(const std::string& n, int v)        { writePushConstant(n, v); }
void VulkanShaderProgram::setUniform1f(const std::string& n, float v)      { writePushConstant(n, v); }
void VulkanShaderProgram::setUniform2i(const std::string& n, int x, int y) {
    int v[2] = {x, y}; registerPushConstant(n, 8);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, v, 8);
}
void VulkanShaderProgram::setUniform3i(const std::string& n, int x, int y, int z) {
    int v[3] = {x, y, z}; registerPushConstant(n, 12);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, v, 12);
}
void VulkanShaderProgram::setUniform4i(const std::string& n, int x, int y, int z, int w) {
    int v[4] = {x, y, z, w}; registerPushConstant(n, 16);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, v, 16);
}
void VulkanShaderProgram::setUniform2f(const std::string& n, float x, float y) {
    float v[2] = {x, y}; registerPushConstant(n, 8);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, v, 8);
}
void VulkanShaderProgram::setUniform3f(const std::string& n, float x, float y, float z) {
    float v[3] = {x, y, z}; registerPushConstant(n, 12);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, v, 12);
}
void VulkanShaderProgram::setUniform4f(const std::string& n, float x, float y, float z, float w) {
    float v[4] = {x, y, z, w}; registerPushConstant(n, 16);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, v, 16);
}
void VulkanShaderProgram::setUniformMat3(const std::string& n, const float* m) {
    registerPushConstant(n, 36);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, m, 36);
}
void VulkanShaderProgram::setUniformMat4(const std::string& n, const float* m) {
    registerPushConstant(n, 64);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, m, 64);
}
void VulkanShaderProgram::setUniform1fv(const std::string& n, const float* values, uint32_t count) {
    uint32_t bytes = count * 4; registerPushConstant(n, bytes);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, values, bytes);
}
void VulkanShaderProgram::setUniform4fv(const std::string& n, const float* values, uint32_t count) {
    uint32_t bytes = count * 16; registerPushConstant(n, bytes);
    auto it = m_pcOffsets.find(n); if (it != m_pcOffsets.end()) std::memcpy(m_pcData.data() + it->second, values, bytes);
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_VULKAN
