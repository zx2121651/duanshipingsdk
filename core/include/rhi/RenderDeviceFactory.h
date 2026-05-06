#pragma once
/**
 * RenderDeviceFactory.h
 *
 * 运行时 RHI 后端工厂。
 *
 * 根据 BackendType + GLContextManager 能力报告，创建并返回正确的
 * IRenderDevice 实现：
 *
 *   GLES   → GLRenderDevice（Tier 0/1/2 根据 GLESVersion 自动选择特性集）
 *   VULKAN → VulkanRenderDevice（HAS_VULKAN 编译宏 + 运行时 libvulkan.so 检测）
 *   METAL  → MetalRenderDevice （HAS_METAL 编译宏，仅 iOS/macOS）
 *   AUTO   → 按优先级：Metal > Vulkan > GLES
 */

#include "IRenderDevice.h"
#include "../GLContextManager.h"
#include <memory>
#include <string>

namespace sdk {
namespace video {
namespace rhi {

/// RHI 后端类型
enum class BackendType {
    AUTO   = 0,  ///< 自动选择（优先级：Metal > Vulkan > GLES）
    GLES   = 1,  ///< OpenGL ES（3.0 / 3.1 / 3.2 三级梯级）
    VULKAN = 2,  ///< Vulkan（Android NDK，需要 HAS_VULKAN）
    METAL  = 3   ///< Metal（iOS / macOS，需要 HAS_METAL）
};

/// BackendType 转可读字符串（日志用）
inline const char* backendTypeName(BackendType t) {
    switch (t) {
        case BackendType::AUTO:   return "AUTO";
        case BackendType::GLES:   return "GLES";
        case BackendType::VULKAN: return "VULKAN";
        case BackendType::METAL:  return "METAL";
        default:                  return "UNKNOWN";
    }
}

class RenderDeviceFactory {
public:
    /**
     * 创建 IRenderDevice 实例。
     *
     * @param preferred    用户首选后端（AUTO = 自动决策）
     * @param ctxManager   GLContextManager（已调用 sniffCapabilities()）
     * @param[out] chosen  实际选择的后端类型（供上层查询）
     * @return             IRenderDevice 实例；失败时返回 GLES 降级实例（不返回 nullptr）
     */
    static std::shared_ptr<IRenderDevice> create(
        BackendType preferred,
        const GLContextManager& ctxManager,
        BackendType& chosen);

    /// 无输出参数版（常用快捷方式）
    static std::shared_ptr<IRenderDevice> create(
        BackendType preferred,
        const GLContextManager& ctxManager)
    {
        BackendType ignored;
        return create(preferred, ctxManager, ignored);
    }
};

} // namespace rhi
} // namespace video
} // namespace sdk
