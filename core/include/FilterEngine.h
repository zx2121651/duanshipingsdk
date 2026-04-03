#pragma once
#include "GLTypes.h"
#include "Filter.h"
#include "FrameBufferPool.h"
#include "ThreadCheck.h"
#include "GLContextManager.h"
#include <memory>
#include <string>
#include <vector>
#include <any>

namespace sdk {
namespace video {

class FilterEngine {
public:
    FilterEngine();
    ~FilterEngine();

    // Init context and setup standard filters. Call this on the GL thread.
    Result initialize();

    // Process a frame through the pipeline
    Texture processFrame(const Texture& textureIn, int width, int height);

    // Update filter parameters
    void updateParameter(const std::string& key, const std::any& value);

    // Release resources
    void release();

    // Pipeline manipulation
    void addFilter(FilterPtr filter);
    void removeAllFilters();

    // 暴露 FBO 内存池供子滤镜（如 Two-pass 高斯模糊）借用
    FrameBufferPool m_frameBufferPool;

    // 暴露 ContextManager 给 NativeBridge 探测
    GLContextManager& getContextManager() { return m_contextManager; }

private:
    std::vector<FilterPtr> m_filters;
    ThreadCheck m_threadCheck;
    bool m_initialized;
    bool m_simulateCrash;

    // 【三级防线】特征级嗅探器
    GLContextManager m_contextManager;
};

using FilterEnginePtr = std::shared_ptr<FilterEngine>;

} // namespace video
} // namespace sdk
