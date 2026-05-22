#pragma once
#include <memory>
#include "Filter.h"
#include "Filters.h"
#include "PropOverlayFilter.h"
#include "GLContextManager.h"
#include "FrameBufferPool.h"

namespace sdk {
namespace video {

// Align with Java/Swift Enums
enum class FilterType {
    BRIGHTNESS = 0,
    GAUSSIAN_BLUR = 1,
    LOOKUP = 2,
    BILATERAL = 3,
    CINEMATIC_LOOKUP = 4,
    COMPUTE_BLUR = 5,
    NIGHT_VISION = 6,
    LUT3D = 7,          // P1-2: 64x64x64 3D LUT colour grading
    DUAL_KAWASE_BLUR = 8, // Iterative Kawase blur — large-radius, low-cost
    BLOOM = 9,            // Glow: threshold → Kawase blur → additive composite
    PROP_OVERLAY = 10     // Real-time prop/sticker overlay (RGBA sprite, alpha-blended)
};

class FilterFactory {
public:
    static FilterPtr createFilter(FilterType type, const GLContextManager& contextManager, FrameBufferPool* pool, std::shared_ptr<rhi::IRenderDevice> device = nullptr) {
        switch (type) {
            case FilterType::BRIGHTNESS:
                return std::make_shared<BrightnessFilter>();

            case FilterType::GAUSSIAN_BLUR:
            case FilterType::COMPUTE_BLUR:
                // [智能降级路由]:
                // 如果上层请求了高斯模糊或者强制请求 Compute Blur，
                // 我们在底层统一根据硬件探测结果进行安全分发。
#ifdef __ANDROID__
                if (contextManager.isComputeShaderSupported()) {
                    return std::make_shared<ComputeBlurFilter>();
                }
#endif
                // 如果不支持 Compute Shader（如 iOS 或低端安卓），安全降级回 Two-Pass FBO 模糊
                return std::make_shared<GaussianBlurFilter>(pool);

            case FilterType::LOOKUP:
                return std::make_shared<LookupFilter>();

            case FilterType::BILATERAL:
                return std::make_shared<BilateralFilter>();

            case FilterType::CINEMATIC_LOOKUP:
                return std::make_shared<CinematicLookupFilter>();

            case FilterType::NIGHT_VISION: {
                auto filter = std::make_shared<NightVisionFilter>();
                filter->setRenderDevice(device);
                return filter;
            }

            case FilterType::LUT3D:
                return std::make_shared<LUT3DFilter>();

            case FilterType::DUAL_KAWASE_BLUR:
                return std::make_shared<DualKawaseBlurFilter>(pool);

            case FilterType::BLOOM:
                return std::make_shared<BloomFilter>(pool);

            case FilterType::PROP_OVERLAY:
                return std::make_shared<PropOverlayFilter>();

            default:
                return nullptr;
        }
    }
};

} // namespace video
} // namespace sdk
