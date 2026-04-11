#pragma once

#include <vector>
#include <deque>
#include <mutex>
#include <chrono>
#include <algorithm>

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

        metrics.p50FrameTimeMs = sorted[static_cast<size_t>(sorted.size() * 0.50)];
        metrics.p90FrameTimeMs = sorted[static_cast<size_t>(sorted.size() * 0.90)];
        metrics.p99FrameTimeMs = sorted[static_cast<size_t>(sorted.size() * 0.99)];

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
