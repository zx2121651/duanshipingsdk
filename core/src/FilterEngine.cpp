#include "../include/FilterEngine.h"
#include <iostream>
#include <algorithm>

namespace sdk {
namespace video {

FilterEngine::FilterEngine() : m_initialized(false), m_simulateCrash(false) {}

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

    // --- 故障模拟器拦截 (Crash Simulation) ---
    // 一旦触发，主动返回非法纹理 ID (0)，模拟底层渲染引擎崩溃或显存耗尽。
    // 这将触发上层 VideoFilterManager 的 DEGRADED 状态并 Bypass 原图。
    if (m_simulateCrash) {
        return Texture{0, static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    }

    if (!m_initialized) {
        std::cerr << "FilterEngine not initialized!" << std::endl;
        return textureIn;
    }

    if (m_filters.empty()) {
        return textureIn; // Passthrough
    }

    Texture currentTexture = textureIn;
    FrameBufferPtr previousFb = nullptr;

    // --- 精准回收的 Ping-Pong FBO 架构 ---
    for (size_t i = 0; i < m_filters.size(); ++i) {
        // 对于最后一步之前的所有中间过程，我们使用 RGB565 格式的半带宽 FBO 即可，这省了一半显存带宽！
        // 如果是最后一步，出于兼容上屏或后续视频硬编码的要求，可能需要标准的 RGBA FBO。
        bool isIntermediate = (i < m_filters.size() - 1);

        FrameBufferPtr targetFb = m_frameBufferPool.getFrameBuffer(width, height, isIntermediate);

        Texture outTexture = m_filters[i]->processFrame(currentTexture, targetFb);
        currentTexture = outTexture;

        // 【极致优化】：当前 Filter 执行完毕后，上一趟的输入 (previousFb) 已经没用了！
        // 此时我们立刻强制将其归还回 Pool 中，这就保证了整个长达 10 几个 Filter 的链条中，
        // 真正在显存里被霸占的 FBO 永远只有 2 个！极大地降低了显存爆表风险。
        if (previousFb != nullptr) {
            m_frameBufferPool.returnFrameBuffer(previousFb.get());
        }

        previousFb = targetFb;
    }

    // 返回最后一趟生成的纹理。由于 shared_ptr custom deleter 的设计（或我们外部的手动管理），
    // 最终业务层拿去渲染后，如果不保留引用，这个最后的 FBO 也会安全回到池子里。
    return currentTexture;
}

void FilterEngine::updateParameter(const std::string& key, const std::any& value) {
    if (key == "simulateCrash") {
        try {
            float val = std::any_cast<float>(value);
            m_simulateCrash = (val > 0.5f);
        } catch (...) {}
        return;
    }

    for (auto& filter : m_filters) {
        filter->setParameter(key, value);
    }
}

void FilterEngine::release() {
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
