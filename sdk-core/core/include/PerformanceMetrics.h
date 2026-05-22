#pragma once

#include <vector>
#include <deque>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace sdk {
namespace video {

struct PerformanceMetrics {
    float averageFrameTimeMs = 0.0f;
    float p50FrameTimeMs = 0.0f;
    float p90FrameTimeMs = 0.0f;
    float p99FrameTimeMs = 0.0f;
    int droppedFrames = 0;
};

class MetricsCollector {
public:
    MetricsCollector(size_t windowSize = 100) : m_windowSize(windowSize) {}

    void recordFrameTime(float timeMs) {
        if (m_windowSize == 0) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_frameTimesMs.size() >= m_windowSize) {
            m_frameTimesMs.pop_front();
        }
        m_frameTimesMs.push_back(timeMs);
    }

    void recordDroppedFrame() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_droppedFrames++;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameTimesMs.clear();
        m_droppedFrames = 0;
    }

    PerformanceMetrics getMetrics() {
        std::lock_guard<std::mutex> lock(m_mutex);
        PerformanceMetrics metrics;
        metrics.droppedFrames = m_droppedFrames;

        if (m_frameTimesMs.empty()) {
            return metrics;
        }

        std::vector<float> sorted(m_frameTimesMs.begin(), m_frameTimesMs.end());
        std::sort(sorted.begin(), sorted.end());

        float sum = 0;
        for (float t : sorted) {
            sum += t;
        }
        metrics.averageFrameTimeMs = sum / sorted.size();

        auto getPercentile = [&](float p) {
            size_t idx = static_cast<size_t>(std::floor(p * (sorted.size() - 1)));
            return sorted[idx];
        };

        metrics.p50FrameTimeMs = getPercentile(0.50f);
        metrics.p90FrameTimeMs = getPercentile(0.90f);
        metrics.p99FrameTimeMs = getPercentile(0.99f);

        return metrics;
    }

private:
    size_t m_windowSize;
    std::deque<float> m_frameTimesMs;
    int m_droppedFrames = 0;
    std::mutex m_mutex;
};

} // namespace video
} // namespace sdk
