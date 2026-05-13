#pragma once
#ifdef HAS_VULKAN

#include "../../../include/rhi/IShaderProgram.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <string>

namespace sdk {
namespace video {
namespace rhi {

/**
 * VulkanShaderProgram — 持有一组 VkShaderModule（vert + frag，可选 geom/tesc/tese/comp）。
 * SPIR-V 字节码通过 createFromSPIRV 或内嵌的 uint32_t[] 数组传入。
 * Uniform 设置通过 push constants 实现（映射到名字 → offset）。
 */
class VulkanShaderProgram : public IShaderProgram {
public:
    struct StageSource {
        ShaderStage     stage;
        std::vector<uint32_t> spirv; // SPIR-V 字节码
    };

    /// 从 SPIR-V 字节码数组创建
    static std::shared_ptr<VulkanShaderProgram> createFromSPIRV(
        VkDevice device, const std::vector<StageSource>& stages);

    ~VulkanShaderProgram() override;

    bool isValid() const override { return !m_modules.empty(); }

    // Uniform setters — 映射到 push constant 写入（由 VulkanCommandBuffer 在 bind 时发送）
    void setUniform1i(const std::string& name, int value) override;
    void setUniform2i(const std::string& name, int x, int y) override;
    void setUniform3i(const std::string& name, int x, int y, int z) override;
    void setUniform4i(const std::string& name, int x, int y, int z, int w) override;
    void setUniform1f(const std::string& name, float value) override;
    void setUniform2f(const std::string& name, float x, float y) override;
    void setUniform3f(const std::string& name, float x, float y, float z) override;
    void setUniform4f(const std::string& name, float x, float y, float z, float w) override;
    void setUniformMat3(const std::string& name, const float* matrix3x3) override;
    void setUniformMat4(const std::string& name, const float* matrix4x4) override;
    void setUniform1fv(const std::string& name, const float* values, uint32_t count) override;
    void setUniform4fv(const std::string& name, const float* values, uint32_t count) override;
    void bind()   override {}
    void unbind() override {}

    // ---- Vulkan-specific accessors ----
    struct Module { VkShaderStageFlagBits stage; VkShaderModule module; };
    const std::vector<Module>& getModules() const { return m_modules; }

    /// Push constant data (max 128 bytes, 32 floats)
    const uint8_t* pushConstantData()  const { return m_pcData.data(); }
    uint32_t       pushConstantSize()  const { return static_cast<uint32_t>(m_pcData.size()); }

private:
    VulkanShaderProgram() = default;
    VkDevice             m_device = VK_NULL_HANDLE;
    std::vector<Module>  m_modules;

    // Push constant block (128 bytes)
    std::vector<uint8_t> m_pcData;
    std::unordered_map<std::string, uint32_t> m_pcOffsets;
    uint32_t m_pcCursor = 0;

    void registerPushConstant(const std::string& name, uint32_t size);
    template<typename T>
    void writePushConstant(const std::string& name, const T& val);
};

/// Translate VkShaderStageFlagBits from ShaderStage enum
inline VkShaderStageFlagBits toVkStage(ShaderStage s) {
    switch (s) {
        case ShaderStage::Vertex:      return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment:    return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Compute:     return VK_SHADER_STAGE_COMPUTE_BIT;
        case ShaderStage::Geometry:    return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::TessEval:    return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        default:                       return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
