#ifndef LOG_TAG
#define LOG_TAG "BodyPoseDetector"
#endif
#include "../../include/ai/BodyPoseDetector.h"
#include "../../include/Log.h"
#include <chrono>
#include <cstring>
#include <numeric>
#include <cmath>

#ifdef HAS_TFLITE
#include "tensorflow/lite/c/c_api.h"
#ifdef HAS_TFLITE_GPU_DELEGATE
#include "tensorflow/lite/delegates/gpu/delegate.h"
#endif
#endif

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// Pimpl — TFLite resources
// ---------------------------------------------------------------------------
struct BodyPoseDetector::Impl {
#ifdef HAS_TFLITE
    std::unique_ptr<tflite::FlatBufferModel>           model;
    std::unique_ptr<tflite::Interpreter>               interpreter;
    tflite::ops::builtin::BuiltinOpResolver            resolver;
    std::vector<uint8_t>                               modelBuffer; // owns buffer load
#endif
};

// ---------------------------------------------------------------------------
BodyPoseDetector::BodyPoseDetector() : m_impl(std::make_unique<Impl>()) {}

BodyPoseDetector::~BodyPoseDetector() {
    release();
}

void BodyPoseDetector::release() {
    m_stopThread.store(true);
    if (m_detectThread.joinable()) m_detectThread.join();
    m_loaded = false;
#ifdef HAS_TFLITE
    m_impl->interpreter.reset();
    m_impl->model.reset();
    m_impl->modelBuffer.clear();
#endif
}

// ---------------------------------------------------------------------------
// Model loading
// ---------------------------------------------------------------------------
bool BodyPoseDetector::loadModel(const std::string& modelPath) {
#ifdef HAS_TFLITE
    m_impl->model = TfLiteModelCreateFromFile(modelPath.c_str());
    if (!m_impl->model) {
        LOGE("BodyPoseDetector: failed to load model from %s", modelPath.c_str());
        return false;
    }
    if (!buildInterpreterInternal()) return false;
    m_loaded = true;
    startDetectThread();
    LOGI("BodyPoseDetector: model loaded from %s", modelPath.c_str());
    return true;
#else
    LOGW("BodyPoseDetector: HAS_TFLITE not defined; using stub mode");
    m_loaded = true;  // stub mode: always "loaded"
    startDetectThread();
    return true;
#endif
}

bool BodyPoseDetector::loadModelFromBuffer(const void* data, size_t size) {
#ifdef HAS_TFLITE
    m_impl->modelBuffer.assign(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + size);
    m_impl->model = TfLiteModelCreate(m_impl->modelBuffer.data(), m_impl->modelBuffer.size());
    if (!m_impl->model) {
        LOGE("BodyPoseDetector: failed to build model from buffer");
        return false;
    }
    if (!buildInterpreterInternal()) return false;
    m_loaded = true;
    startDetectThread();
    return true;
#else
    (void)data; (void)size;
    LOGW("BodyPoseDetector: HAS_TFLITE not defined; using stub mode");
    m_loaded = true;
    startDetectThread();
    return true;
#endif
}

#ifdef HAS_TFLITE
bool BodyPoseDetector::buildInterpreterInternal() {
    m_impl->options = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(m_impl->options, 2);

#ifdef HAS_TFLITE_GPU_DELEGATE
    TfLiteGpuDelegateOptionsV2 opts = TfLiteGpuDelegateOptionsV2Default();
    opts.inference_preference = TFLITE_GPU_INFERENCE_PREFERENCE_FAST_SINGLE_ANSWER;
    m_impl->gpuDelegate = TfLiteGpuDelegateV2Create(&opts);
    if (m_impl->gpuDelegate) {
        TfLiteInterpreterOptionsAddDelegate(m_impl->options, m_impl->gpuDelegate);
    } else {
        LOGW("BodyPoseDetector: GPU delegate failed, falling back to CPU");
    }
#endif

    m_impl->interpreter = TfLiteInterpreterCreate(m_impl->model, m_impl->options);
    if (!m_impl->interpreter) return false;

    if (TfLiteInterpreterAllocateTensors(m_impl->interpreter) != kTfLiteOk) {
        LOGE("BodyPoseDetector: AllocateTensors failed");
        return false;
    }

    const TfLiteTensor* inputTensor = TfLiteInterpreterGetInputTensor(m_impl->interpreter, 0);
    m_impl->inputW = TfLiteTensorDim(inputTensor, 2);
    m_impl->inputH = TfLiteTensorDim(inputTensor, 1);

    LOGI("BodyPoseDetector: model input %dx%d", m_impl->inputW, m_impl->inputH);
    m_impl->ready = true;
    m_loaded = true;
    startDetectThread();
    return true;
}
#endif

// ---------------------------------------------------------------------------
// Async submission
// ---------------------------------------------------------------------------
void BodyPoseDetector::submitFrame(const uint8_t* rgba, int w, int h,
                                   int64_t timestampNs) {
    if (!m_loaded) return;
    std::lock_guard<std::mutex> lk(m_frameMutex);
    m_pendingFrame.pixels.assign(rgba, rgba + w * h * 4);
    m_pendingFrame.width       = w;
    m_pendingFrame.height      = h;
    m_pendingFrame.timestampNs = timestampNs;
    m_framePending.store(true);
}

PoseFrameResult BodyPoseDetector::runSync(const uint8_t* rgba, int w, int h) {
    PendingFrame f;
    f.pixels.assign(rgba, rgba + w * h * 4);
    f.width       = w;
    f.height      = h;
    f.timestampNs = std::chrono::steady_clock::now().time_since_epoch().count();
    return runInferenceInternal(f);
}

PoseFrameResult BodyPoseDetector::getLatestResult() const {
    std::lock_guard<std::mutex> lk(m_resultMutex);
    return m_latestResult;
}

void BodyPoseDetector::setResultCallback(
    std::function<void(const PoseFrameResult&)> cb) {
    m_callback = std::move(cb);
}

// ---------------------------------------------------------------------------
// Background detect thread
// ---------------------------------------------------------------------------
void BodyPoseDetector::startDetectThread() {
    m_stopThread.store(false);
    m_detectThread = std::thread(&BodyPoseDetector::detectLoop, this);
}

void BodyPoseDetector::detectLoop() {
    while (!m_stopThread.load()) {
        if (!m_framePending.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }

        PendingFrame frame;
        {
            std::lock_guard<std::mutex> lk(m_frameMutex);
            frame = m_pendingFrame;
            m_framePending.store(false);
        }

        auto result = runInferenceInternal(frame);

        {
            std::lock_guard<std::mutex> lk(m_resultMutex);
            m_latestResult = result;
        }
        if (m_callback) m_callback(result);
    }
}

// ---------------------------------------------------------------------------
// Inference dispatch
// ---------------------------------------------------------------------------
PoseFrameResult BodyPoseDetector::runInferenceInternal(const PendingFrame& frame) {
    auto t0 = std::chrono::steady_clock::now();
    auto result = runTFLite(frame.pixels.data(),
                            frame.width, frame.height,
                            frame.timestampNs);
    float ms = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    result.inferenceTimeMs = ms;

    // exponential moving average for perf stats
    m_avgInferenceMs = (m_inferenceCount == 0)
        ? ms
        : 0.9f * m_avgInferenceMs + 0.1f * ms;
    ++m_inferenceCount;
    return result;
}

// ---------------------------------------------------------------------------
// TFLite inference (real path guarded by HAS_TFLITE; otherwise stub)
// ---------------------------------------------------------------------------
PoseFrameResult BodyPoseDetector::runTFLite(const uint8_t* rgba,
                                             int w, int h, int64_t ts) {
    PoseFrameResult result;
    result.frameTimestampNs = ts;

#ifdef HAS_TFLITE
    if (!m_impl->interpreter) return result;

    // MoveNet Lightning input: uint8 [1, 192, 192, 3]
    constexpr int kInputSize = 192;
    auto* inputTensor = m_impl->interpreter->input_tensor(0);
    if (!inputTensor || inputTensor->bytes < kInputSize * kInputSize * 3) return result;

    // Bilinear downsample + channel drop (RGBA → RGB)
    uint8_t* inp = inputTensor->data.uint8;
    for (int row = 0; row < kInputSize; ++row) {
        float srcY = row * (float)(h - 1) / (kInputSize - 1);
        int y0 = static_cast<int>(srcY);
        for (int col = 0; col < kInputSize; ++col) {
            float srcX = col * (float)(w - 1) / (kInputSize - 1);
            int x0 = static_cast<int>(srcX);
            int offset = (y0 * w + x0) * 4;
            inp[(row * kInputSize + col) * 3 + 0] = rgba[offset + 0];
            inp[(row * kInputSize + col) * 3 + 1] = rgba[offset + 1];
            inp[(row * kInputSize + col) * 3 + 2] = rgba[offset + 2];
        }
    }

    if (TfLiteInterpreterInvoke(m_impl->interpreter) != kTfLiteOk) return result;

    // MoveNet output: float32 [1, 1, 17, 3] — (y, x, score)
    auto* out = m_impl->interpreter->output_tensor(0);
    if (!out) return result;
    result.pose = decodeKeypoints(out->data.f, w, h);
    result.pose.detected = (result.pose.poseScore >= 0.15f);
#else
    // Stub: return a plausible T-pose for CI testing
    result.pose.detected = false; // no real inference without model
    (void)rgba; (void)w; (void)h;
#endif
    return result;
}

// ---------------------------------------------------------------------------
// Decode keypoints from MoveNet/BlazePose output
// ---------------------------------------------------------------------------
PoseResult BodyPoseDetector::decodeKeypoints(const float* output,
                                              int w, int h) {
    PoseResult pose;
    if (!output) {
        pose.detected = false;
        return pose;
    }

    float scoreSum = 0.f;
    // Current MoveNet-style: 17 points [y, x, score]
    // TODO: Reserved for 33-point BlazePose extension (indices 17-32)
    for (int i = 0; i < kPoseKeypointCount; ++i) {
        // MoveNet: output[i*3+0] = y_norm, [i*3+1] = x_norm, [i*3+2] = score
        pose.keypoints[i].y     = output[i * 3 + 0] * (float)h;
        pose.keypoints[i].x     = output[i * 3 + 1] * (float)w;
        pose.keypoints[i].score = output[i * 3 + 2];
        scoreSum += pose.keypoints[i].score;
    }
    pose.poseScore = scoreSum / kPoseKeypointCount;
    // Basic threshold for "detected" state
    pose.detected  = (pose.poseScore >= 0.15f);
    return pose;
}

} // namespace ai
} // namespace video
} // namespace sdk
