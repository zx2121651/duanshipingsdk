#pragma once
#include "GLTypes.h"
#include "Filter.h"
#include "FrameBufferPool.h"
#include "ThreadCheck.h"
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

private:
    std::vector<FilterPtr> m_filters;
    ThreadCheck m_threadCheck;
    public:
    FrameBufferPool m_frameBufferPool;
private:
    bool m_initialized;
};

using FilterEnginePtr = std::shared_ptr<FilterEngine>;

} // namespace video
} // namespace sdk
