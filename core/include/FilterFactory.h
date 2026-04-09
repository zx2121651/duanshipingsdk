#pragma once
#include <memory>
#include "Filter.h"
#include "Filters.h"
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
    COMPUTE_BLUR = 5
};

class FilterFactory {
public:
    static FilterPtr createFilter(FilterType type, const GLContextManager& contextManager, FrameBufferPool* pool) {
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

            default:
                return nullptr;
        }
    }
};

} // namespace video
} // namespace sdk
