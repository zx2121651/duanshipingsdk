#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>

namespace sdk {
namespace video {
namespace ai {

// Prediction result carrier (immutable object)
struct FaceLandmarks {
    int64_t timestampNs = 0;
    std::vector<float> points; // 106 or 240 points (x, y) normalized coordinates
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
};

class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    // Called by the render thread: submits a downscaled CPU image frame for AI analysis (non-blocking)
    // Warning: The image passed here should be downscaled (e.g., 180x320)
    void submitFrame(int64_t timestampNs, const std::vector<uint8_t>& downscaledRgba);

    // Called by the render thread: retrieves the *latest* prediction result (non-blocking, lightweight lock)
    // If prediction for the current timestampNs is not yet complete, this returns the last valid result.
    std::shared_ptr<FaceLandmarks> getLatestLandmarks();

private:
    void loop(); // Resident background thread function

    std::thread m_workerThread;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;

    // Pending task queue (capacity restricted to 1, drops expired frames)
    struct Task {
        int64_t ts;
        std::vector<uint8_t> data;
    };
    std::vector<Task> m_taskQueue;

    // Latest result double buffering
    std::mutex m_resultMutex;
    std::shared_ptr<FaceLandmarks> m_latestResult;

    bool m_stop = false;
};

} // namespace ai
} // namespace video
} // namespace sdk
