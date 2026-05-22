#include "../../include/ai/InferenceEngine.h"
#include <iostream>
#include <chrono>

#define LOG_TAG "InferenceEngine"
#include "../../include/Log.h"

namespace sdk {
namespace video {
namespace ai {

InferenceEngine::InferenceEngine() {
    m_stop = false;
    m_workerThread = std::thread(&InferenceEngine::loop, this);
}

InferenceEngine::~InferenceEngine() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    m_cv.notify_one();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void InferenceEngine::submitFrame(int64_t timestampNs, const std::vector<uint8_t>& downscaledRgba) {
    std::lock_guard<std::mutex> lock(m_queueMutex);

    // Capacity = 1, always overwrite with the freshest frame and drop the old one
    m_taskQueue.clear();
    m_taskQueue.push_back({timestampNs, downscaledRgba});

    m_cv.notify_one();
}

std::shared_ptr<FaceLandmarks> InferenceEngine::getLatestLandmarks() {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    return m_latestResult;
}

void InferenceEngine::loop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this] { return m_stop || !m_taskQueue.empty(); });

            if (m_stop) {
                break;
            }

            // Pop the single freshest task
            task = std::move(m_taskQueue.front());
            m_taskQueue.clear();
        }

        // ---------------------------------------------------------
        // [AI Inference Simulation]
        // This is where actual NN execution (TFLite/MNN) would happen.
        // We simulate a heavy 15-30ms workload.
        // ---------------------------------------------------------
        // e.g., std::this_thread::sleep_for(std::chrono::milliseconds(15));

        auto landmarks = std::make_shared<FaceLandmarks>();
        landmarks->timestampNs = task.ts;
        // Mock points: normally 106 points for face mesh (x, y)
        // We put a dummy mesh to satisfy vertex upload requirements later
        landmarks->points.assign(106 * 2, 0.5f);
        landmarks->pitch = 0.0f;
        landmarks->yaw = 0.0f;
        landmarks->roll = 0.0f;

        // ---------------------------------------------------------
        // Update the double buffer holding the latest result
        // ---------------------------------------------------------
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_latestResult = landmarks;
        }
    }
}

} // namespace ai
} // namespace video
} // namespace sdk
