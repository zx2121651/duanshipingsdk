#pragma once
/**
 * FaceLandmarkDetector.h
 *
 * Realtime 106-point face landmark detector for beauty, reshape, makeup, and
 * AR effects. All public landmark coordinates are normalized [0,1] and aligned
 * with GL texture coordinates.
 */

#include "../GLTypes.h"
#include "TfliteInferenceEngine.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sdk {
namespace video {
namespace ai {

static constexpr int kFaceLandmarkCount = 106;
static constexpr int kMaxFaces = 2;

struct FaceLandmark {
    float x = 0.f;     ///< Normalized x in [0,1], aligned with GL texture coordinates.
    float y = 0.f;     ///< Normalized y in [0,1], aligned with GL texture coordinates.
    float score = 0.f; ///< Confidence [0,1].
};

struct FaceResult {
    bool detected = false;
    float faceScore = 0.f;
    float boundingBox[4] = {}; ///< Normalized [x, y, w, h].
    std::array<FaceLandmark, kFaceLandmarkCount> landmarks{};

    FaceLandmark chin() const { return landmarks[8]; }
    FaceLandmark leftCheek() const { return landmarks[0]; }
    FaceLandmark rightCheek() const { return landmarks[16]; }
    FaceLandmark noseTip() const { return landmarks[30]; }
    FaceLandmark leftEye() const { return landmarks[39]; }
    FaceLandmark rightEye() const { return landmarks[42]; }
    FaceLandmark mouthLeft() const { return landmarks[48]; }
    FaceLandmark mouthRight() const { return landmarks[54]; }
    FaceLandmark forehead() const { return landmarks[27]; }
};

struct LandmarkFrameResult {
    int faceCount = 0;
    std::array<FaceResult, kMaxFaces> faces{};
    int64_t frameTimestampNs = 0;
    float inferenceTimeMs = 0.f;
};

class FaceLandmarkDetector {
public:
    FaceLandmarkDetector();
    ~FaceLandmarkDetector();

    FaceLandmarkDetector(const FaceLandmarkDetector&) = delete;
    FaceLandmarkDetector& operator=(const FaceLandmarkDetector&) = delete;

    bool loadModel(const std::string& modelPath);
    bool loadModelFromBuffer(const void* data, size_t size);
    bool isLoaded() const { return m_loaded; }
    void release();

    void setDelegateHint(TfliteInferenceEngine::DelegateHint hint) {
        m_delegateHint = hint;
    }
    TfliteInferenceEngine::DelegateHint getDelegateHint() const {
        return m_delegateHint;
    }

    void submitFrame(const uint8_t* rgbaPixels, int width, int height,
                     int64_t timestampNs);
    LandmarkFrameResult runSync(const uint8_t* rgbaPixels, int width, int height);
    LandmarkFrameResult getLatestResult() const;

    void setResultCallback(std::function<void(const LandmarkFrameResult&)> cb);
    float getAverageInferenceMs() const;

    static void decodeLandmarks(const float* rawOutput, int outputLen,
                                int imgW, int imgH, FaceResult& out);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_loaded = false;

    mutable std::mutex m_resultMutex;
    LandmarkFrameResult m_latestResult;

    std::function<void(const LandmarkFrameResult&)> m_callback;

    std::thread m_detectThread;
    std::atomic<bool> m_stopThread{false};
    std::atomic<bool> m_framePending{false};

    struct PendingFrame {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        int64_t timestampNs = 0;
    };
    mutable std::mutex m_frameMutex;
    PendingFrame m_pendingFrame;

    TfliteInferenceEngine::DelegateHint m_delegateHint =
        TfliteInferenceEngine::DelegateHint::GPU;

    float m_avgInferenceMs = 0.f;
    int m_inferenceCount = 0;

    void startDetectThread();
    void detectLoop();
    LandmarkFrameResult runInferenceInternal(const PendingFrame& frame);
    LandmarkFrameResult runTFLite(const uint8_t* rgba, int w, int h, int64_t ts);

#ifdef HAS_TFLITE
    bool buildInterpreterInternal();
#endif
};

} // namespace ai
} // namespace video
} // namespace sdk
