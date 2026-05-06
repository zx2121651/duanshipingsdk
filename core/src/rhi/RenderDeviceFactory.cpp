/**
 * RenderDeviceFactory.cpp
 *
 * 按优先级链选择 RHI 后端：
 *   AUTO  → Metal > Vulkan > GLES
 *   GLES  → GLRenderDevice（强制）
 *   VULKAN→ VulkanRenderDevice（若 HAS_VULKAN 且运行时可用）→ 降级 GLES
 *   METAL → MetalRenderDevice（若 HAS_METAL）→ 降级 GLES
 */

#include "../../include/rhi/RenderDeviceFactory.h"
#include "GLRenderDevice.h"

#ifdef HAS_VULKAN
#   include "vk/VulkanRenderDevice.h"
#endif

#ifdef HAS_METAL
#   include "mtl/MetalRenderDevice.h"
#endif

#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

std::shared_ptr<IRenderDevice> RenderDeviceFactory::create(
    BackendType preferred,
    const GLContextManager& ctxManager,
    BackendType& chosen)
{
    // ----------------------------------------------------------------
    // 解析 AUTO → 具体后端
    // ----------------------------------------------------------------
    BackendType resolved = preferred;
    if (resolved == BackendType::AUTO) {
#if defined(__APPLE__) && defined(HAS_METAL)
        resolved = BackendType::METAL;
#elif defined(__ANDROID__) && defined(HAS_VULKAN)
        resolved = ctxManager.isVulkanSupported() ? BackendType::VULKAN : BackendType::GLES;
#else
        resolved = BackendType::GLES;
#endif
    }

    // ----------------------------------------------------------------
    // Metal
    // ----------------------------------------------------------------
#ifdef HAS_METAL
    if (resolved == BackendType::METAL) {
        auto dev = MetalRenderDevice::tryCreate();
        if (dev) {
            chosen = BackendType::METAL;
            std::cout << "RHI: Backend selected → METAL" << std::endl;
            return dev;
        }
        std::cerr << "RHI: MetalRenderDevice::tryCreate() failed, fallback to GLES" << std::endl;
        resolved = BackendType::GLES;
    }
#else
    if (resolved == BackendType::METAL) {
        std::cerr << "RHI: METAL backend requested but not compiled (HAS_METAL not set), fallback to GLES" << std::endl;
        resolved = BackendType::GLES;
    }
#endif

    // ----------------------------------------------------------------
    // Vulkan
    // ----------------------------------------------------------------
#ifdef HAS_VULKAN
    if (resolved == BackendType::VULKAN) {
        if (ctxManager.isVulkanSupported()) {
            auto dev = VulkanRenderDevice::tryCreate();
            if (dev) {
                chosen = BackendType::VULKAN;
                std::cout << "RHI: Backend selected → VULKAN" << std::endl;
                return dev;
            }
        }
        std::cerr << "RHI: VulkanRenderDevice::tryCreate() failed, fallback to GLES" << std::endl;
        resolved = BackendType::GLES;
    }
#else
    if (resolved == BackendType::VULKAN) {
        std::cerr << "RHI: VULKAN backend requested but not compiled (HAS_VULKAN not set), fallback to GLES" << std::endl;
        resolved = BackendType::GLES;
    }
#endif

    // ----------------------------------------------------------------
    // GLES（最终兜底）
    // ----------------------------------------------------------------
    chosen = BackendType::GLES;
    int tier = ctxManager.getGLESVersionInt();
    std::cout << "RHI: Backend selected → GLES (Tier " << tier << ")" << std::endl;
    return std::make_shared<GLRenderDevice>();
}

} // namespace rhi
} // namespace video
} // namespace sdk
