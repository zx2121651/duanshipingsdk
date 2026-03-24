#include "../include/FilterEngine.h"
#include <iostream>
#include <algorithm>

namespace sdk {
namespace video {

FilterEngine::FilterEngine() : m_initialized(false) {}

FilterEngine::~FilterEngine() {
    release();
}

Result FilterEngine::initialize() {
    m_threadCheck.bind(); // Bind to current (GL) thread.

    for (auto& filter : m_filters) {
        filter->initialize();
    }

    m_initialized = true;
    return Result::ok();
}

Texture FilterEngine::processFrame(const Texture& textureIn, int width, int height) {
    if (!m_threadCheck.check("processFrame must be called on the render thread")) {
        return textureIn;
    }

    if (!m_initialized) {
        std::cerr << "FilterEngine not initialized!" << std::endl;
        return textureIn;
    }

    if (m_filters.empty()) {
        return textureIn; // Passthrough
    }

    Texture currentTexture = textureIn;

    // Ping-pong rendering logic for multiple filters
    FrameBufferPtr fb1 = m_frameBufferPool.getFrameBuffer(width, height);
    FrameBufferPtr fb2 = m_frameBufferPool.getFrameBuffer(width, height);

    bool useFb1 = true;
    for (size_t i = 0; i < m_filters.size(); ++i) {
        FrameBufferPtr targetFb = useFb1 ? fb1 : fb2;
        currentTexture = m_filters[i]->processFrame(currentTexture, targetFb);
        useFb1 = !useFb1;
    }

    // Since custom deleters handle returning framebuffers to the pool,
    // fb1 and fb2 will automatically be returned when they go out of scope.

    return currentTexture;
}

void FilterEngine::updateParameter(const std::string& key, const std::any& value) {
    // Assuming keys are globally unique or meant for a specific filter.
    for (auto& filter : m_filters) {
        filter->setParameter(key, value);
    }
}

void FilterEngine::release() {
    // Note: if release is called from a destructor, thread checks might fail if called from wrong thread,
    // but we can't throw in a destructor. We just clear resources.
    if (m_initialized) {
        for (auto& filter : m_filters) {
            filter->release();
        }
        m_frameBufferPool.clear();
        m_initialized = false;
    }
    m_filters.clear();
}

void FilterEngine::addFilter(FilterPtr filter) {
    if (filter) {
        m_filters.push_back(filter);
        if (m_initialized) {
            filter->initialize();
        }
    }
}

void FilterEngine::removeAllFilters() {
    for (auto& filter : m_filters) {
        if (m_initialized) {
            filter->release();
        }
    }
    m_filters.clear();
}

} // namespace video
} // namespace sdk
