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
#include <cstdlib>
#include <cstring>
#ifdef __ANDROID__
#   include <sys/system_properties.h>
#endif

namespace sdk {
namespace video {
namespace rhi {

std::shared_ptr<IRenderDevice> RenderDeviceFactory::create(
    BackendType preferred,
    const GLContextManager& ctxManager,
    BackendType& chosen)
{
    // ----------------------------------------------------------------
    // QA/debug: runtime backend override via env variable or system prop
    //   setenv SDK_RHI_BACKEND=GLES|VULKAN|METAL  (Linux/macOS/Android)
    //   set    SDK_RHI_BACKEND=GLES               (Windows)
    //   adb shell setprop debug.sdk.rhi.backend GLES  (Android only)
    // Only takes effect when preferred == AUTO.
    // ----------------------------------------------------------------
    if (preferred == BackendType::AUTO) {
        const char* envVal = std::getenv("SDK_RHI_BACKEND");
#ifdef __ANDROID__
        char sysProp[PROP_VALUE_MAX] = {};
        if (!envVal && __system_property_get("debug.sdk.rhi.backend", sysProp) > 0)
            envVal = sysProp;
#endif
        if (envVal && *envVal) {
            if      (!std::strcmp(envVal, "GLES"))   preferred = BackendType::GLES;
            else if (!std::strcmp(envVal, "VULKAN")) preferred = BackendType::VULKAN;
            else if (!std::strcmp(envVal, "METAL"))  preferred = BackendType::METAL;
            // unknown value: ignore, fall through to platform-based AUTO
            if (preferred != BackendType::AUTO)
                std::cout << "RHI: ENV/prop override '" << envVal << "' -> "
                          << backendTypeName(preferred) << std::endl;
        }
    }

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
        auto dev = sdk::video::rhi::MetalRenderDevice::tryCreate();
        if (dev) {
            chosen = BackendType::METAL;
            std::cout << "RHI: Backend selected \u2192 METAL" << std::endl;
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
