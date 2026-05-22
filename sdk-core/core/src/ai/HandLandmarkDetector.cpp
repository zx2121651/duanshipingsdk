/**
 * HandLandmarkDetector.cpp
 *
 * 21 点手部关键点检测实现。
 * HAS_TFLITE 定义时：使用真实 TFLite 推理（hand_landmark.tflite）。
 * 否则：返回全零 stub 结果（保证接口可用，不崩溃）。
 *
 * TFLite 模型 I/O（MediaPipe Hands Lite 格式）：
 *   输入  : [1, 224, 224, 3] float32 (RGB，归一化到 [0,1])
 *   输出0 : [1, 63]  float32  — 21 点坐标展平 (x0,y0,z0, x1,y1,z1, …)，值域 [0,1]
 *   输出1 : [1, 1]   float32  — 手部存在置信度 (sigmoid)
 *   输出2 : [1, 1]   float32  — 左/右手分类（0=左, 1=右）
 */

#include "../../include/ai/HandLandmarkDetector.h"

#define LOG_TAG "HandLandmarkDetector"
#include "../../include/Log.h"

#include <cstring>
#include <chrono>
#include <cmath>

#ifdef HAS_TFLITE
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"
#ifdef __ANDROID__
#include "tensorflow/lite/delegates/gpu/delegate.h"
#include "tensorflow/lite/delegates/nnapi/nnapi_delegate.h"
#endif
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"
#endif

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// Impl (TFLite state — compiled in only when HAS_TFLITE)
// ---------------------------------------------------------------------------
struct HandLandmarkDetector::Impl {
#ifdef HAS_TFLITE
    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter>     interpreter;
    TfLiteDelegate* gpuDelegate      = nullptr;
    TfLiteDelegate* nnapiDelegate    = nullptr;
    TfLiteDelegate* xnnpackDelegate  = nullptr;
    bool            built            = false;
#endif
    // buffer-load path: keep buffer alive
    std::vector<uint8_t> modelBuffer;
};

// ---------------------------------------------------------------------------
HandLandmarkDetector::HandLandmarkDetector()
    : m_impl(std::make_unique<Impl>())
{}

HandLandmarkDetector::~HandLandmarkDetector() {
    release();
}

// ---------------------------------------------------------------------------
// release()
// ---------------------------------------------------------------------------
void HandLandmarkDetector::release() {
    m_stopThread.store(true);
    if (m_detectThread.joinable()) m_detectThread.join();

#ifdef HAS_TFLITE
    m_impl->interpreter.reset();
    m_impl->model.reset();
#   ifdef __ANDROID__
    if (m_impl->gpuDelegate)   { TfLiteGpuDelegateV2Delete(m_impl->gpuDelegate);   m_impl->gpuDelegate = nullptr; }
    if (m_impl->nnapiDelegate) { delete m_impl->nnapiDelegate; m_impl->nnapiDelegate = nullptr; }
#   endif
    if (m_impl->xnnpackDelegate) {
        TfLiteXNNPackDelegateDelete(m_impl->xnnpackDelegate);
        m_impl->xnnpackDelegate = nullptr;
    }
    m_impl->built = false;
#endif
    m_loaded = false;
}

// ---------------------------------------------------------------------------
// loadModel()
// ---------------------------------------------------------------------------
bool HandLandmarkDetector::loadModel(const std::string& modelPath) {
#ifdef HAS_TFLITE
    m_impl->model = tflite::FlatBufferModel::BuildFromFile(modelPath.c_str());
    if (!m_impl->model) {
        m_lastError = "HandLandmarkDetector: failed to load model from " + modelPath;
        LOGE("%s", m_lastError.c_str());
        return false;
    }
    if (!buildInterpreterInternal()) return false;
    m_loaded = true;
    startDetectThread();
    return true;
#else
    (void)modelPath;
    m_lastError = "HandLandmarkDetector: HAS_TFLITE not defined (stub mode)";
    LOGW("%s", m_lastError.c_str());
    m_loaded = true; // allow stub usage
    startDetectThread();
    return true;
#endif
}

bool HandLandmarkDetector::loadModelFromBuffer(const void* data, size_t size) {
#ifdef HAS_TFLITE
    m_impl->modelBuffer.assign(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + size);
    m_impl->model = tflite::FlatBufferModel::BuildFromBuffer(
        reinterpret_cast<const char*>(m_impl->modelBuffer.data()),
        m_impl->modelBuffer.size());
    if (!m_impl->model) {
        m_lastError = "HandLandmarkDetector: failed to build model from buffer";
        return false;
    }
    if (!buildInterpreterInternal()) return false;
    m_loaded = true;
    startDetectThread();
    return true;
#else
    (void)data; (void)size;
    m_loaded = true;
    startDetectThread();
    return true;
#endif
}

// ---------------------------------------------------------------------------
// submitFrame() / runSync()
// ---------------------------------------------------------------------------
void HandLandmarkDetector::submitFrame(const uint8_t* rgbaPixels,
                                       int width, int height,
                                       int64_t timestampNs) {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    auto& pf = m_pendingFrame;
    pf.width       = width;
    pf.height      = height;
    pf.timestampNs = timestampNs;
    pf.pixels.assign(rgbaPixels, rgbaPixels + (size_t)(width * height * 4));
    m_framePending.store(true);
}

HandFrameResult HandLandmarkDetector::runSync(const uint8_t* rgbaPixels,
                                              int width, int height) {
    PendingFrame pf;
    pf.width       = width;
    pf.height      = height;
    pf.timestampNs = 0;
    pf.pixels.assign(rgbaPixels, rgbaPixels + (size_t)(width * height * 4));
    return runInferenceInternal(pf);
}

HandFrameResult HandLandmarkDetector::getLatestResult() const {
    std::lock_guard<std::mutex> lk(m_resultMutex);
    return m_latestResult;
}

void HandLandmarkDetector::setResultCallback(
    std::function<void(const HandFrameResult&)> cb) {
    m_callback = std::move(cb);
}

float HandLandmarkDetector::getAverageInferenceMs() const {
    return m_avgInferenceMs;
}

// ---------------------------------------------------------------------------
// Background detect thread
// ---------------------------------------------------------------------------
void HandLandmarkDetector::startDetectThread() {
    m_stopThread.store(false);
    m_detectThread = std::thread(&HandLandmarkDetector::detectLoop, this);
}

void HandLandmarkDetector::detectLoop() {
    while (!m_stopThread.load()) {
        if (!m_framePending.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }

        PendingFrame frameCopy;
        {
            std::lock_guard<std::mutex> lk(m_frameMutex);
            frameCopy = m_pendingFrame;
            m_framePending.store(false);
        }

        HandFrameResult result = runInferenceInternal(frameCopy);

        {
            std::lock_guard<std::mutex> lk(m_resultMutex);
            m_latestResult = result;
        }

        if (m_callback) m_callback(result);

        // running average
        m_inferenceCount++;
        float alpha = (m_inferenceCount < 30) ? 1.0f / m_inferenceCount : 0.05f;
        m_avgInferenceMs += alpha * (result.inferenceTimeMs - m_avgInferenceMs);
    }
}

// ---------------------------------------------------------------------------
// runInferenceInternal() — dispatches to TFLite or stub
// ---------------------------------------------------------------------------
HandFrameResult HandLandmarkDetector::runInferenceInternal(const PendingFrame& frame) {
    return runTFLite(frame.pixels.data(), frame.width, frame.height, frame.timestampNs);
}

// ---------------------------------------------------------------------------
// decodeHandResult() — float[63] → HandResult
// ---------------------------------------------------------------------------
HandResult HandLandmarkDetector::decodeHandResult(
    const float* coords, int numCoords, float handScore, bool isRight)
{
    HandResult result;
    result.detected    = (handScore >= 0.5f);
    result.handScore   = handScore;
    result.isRightHand = isRight;

    int pointCount = std::min(numCoords / 3, kHandLandmarkCount);
    for (int i = 0; i < pointCount; ++i) {
        result.landmarks[i].x     = coords[i * 3 + 0];
        result.landmarks[i].y     = coords[i * 3 + 1];
        result.landmarks[i].z     = coords[i * 3 + 2];
        result.landmarks[i].score = handScore;
    }
    return result;
}

// ---------------------------------------------------------------------------
// runTFLite() — real impl (HAS_TFLITE) or stub
// ---------------------------------------------------------------------------
HandFrameResult HandLandmarkDetector::runTFLite(
    const uint8_t* rgba, int w, int h, int64_t ts)
{
    HandFrameResult out;
    out.frameTimestampNs = ts;

    auto t0 = std::chrono::steady_clock::now();

#ifdef HAS_TFLITE
    if (!m_impl->built || !m_impl->interpreter) {
        return out;
    }

    // ── 1. Resize RGBA → RGB 224×224 bilinear ────────────────────────────
    constexpr int MODEL_W = 224;
    constexpr int MODEL_H = 224;
    static thread_local std::vector<float> inputBuf(MODEL_W * MODEL_H * 3);

    for (int dy = 0; dy < MODEL_H; ++dy) {
        for (int dx = 0; dx < MODEL_W; ++dx) {
            float sx = (dx + 0.5f) * w  / MODEL_W - 0.5f;
            float sy = (dy + 0.5f) * h / MODEL_H - 0.5f;
            int   ix = std::max(0, std::min(w  - 1, (int)sx));
            int   iy = std::max(0, std::min(h - 1, (int)sy));
            const uint8_t* px = rgba + (iy * w + ix) * 4;
            int idx = (dy * MODEL_W + dx) * 3;
            inputBuf[idx + 0] = px[0] / 255.0f;
            inputBuf[idx + 1] = px[1] / 255.0f;
            inputBuf[idx + 2] = px[2] / 255.0f;
        }
    }

    // ── 2. Fill input tensor ──────────────────────────────────────────────
    float* input = m_impl->interpreter->typed_input_tensor<float>(0);
    if (!input) return out;
    std::memcpy(input, inputBuf.data(), inputBuf.size() * sizeof(float));

    // ── 3. Invoke ─────────────────────────────────────────────────────────
    if (m_impl->interpreter->Invoke() != kTfLiteOk) return out;

    // ── 4. Decode outputs ─────────────────────────────────────────────────
    // Output 0: landmarks [1, 63]
    const float* landmarks0 = m_impl->interpreter->typed_output_tensor<float>(0);
    // Output 1: hand presence score [1, 1]
    const float* score0 = m_impl->interpreter->typed_output_tensor<float>(1);
    // Output 2: handedness [1, 1]  (optional; some models omit it)
    const float* handedness0 = m_impl->interpreter->typed_output_tensor<float>(2);

    float hs = (score0 ? 1.0f / (1.0f + std::exp(-score0[0])) : 0.f);
    bool  isRight = (handedness0 ? handedness0[0] >= 0.5f : true);

    if (hs >= 0.5f && landmarks0) {
        out.hands[0] = decodeHandResult(landmarks0, 63, hs, isRight);
        out.handCount = 1;
    }
#else
    (void)rgba; (void)w; (void)h;
    // stub: return empty result
#endif

    auto t1 = std::chrono::steady_clock::now();
    out.inferenceTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return out;
}

// ---------------------------------------------------------------------------
// buildInterpreterInternal() — HAS_TFLITE only
// ---------------------------------------------------------------------------
#ifdef HAS_TFLITE
bool HandLandmarkDetector::buildInterpreterInternal() {
    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*m_impl->model, resolver);
    builder(&m_impl->interpreter);
    if (!m_impl->interpreter) {
        m_lastError = "HandLandmarkDetector: failed to create interpreter";
        return false;
    }
    m_impl->interpreter->SetNumThreads(2);

    // Apply delegate based on hint
    switch (m_delegateHint) {
#ifdef __ANDROID__
        case TfliteInferenceEngine::DelegateHint::GPU: {
            TfLiteGpuDelegateOptionsV2 opts = TfLiteGpuDelegateOptionsV2Default();
            m_impl->gpuDelegate = TfLiteGpuDelegateV2Create(&opts);
            if (m_impl->gpuDelegate &&
                m_impl->interpreter->ModifyGraphWithDelegate(m_impl->gpuDelegate) != kTfLiteOk) {
                TfLiteGpuDelegateV2Delete(m_impl->gpuDelegate);
                m_impl->gpuDelegate = nullptr;
            }
            break;
        }
        case TfliteInferenceEngine::DelegateHint::NNAPI: {
            m_impl->nnapiDelegate = new tflite::StatefulNnApiDelegate();
            if (m_impl->interpreter->ModifyGraphWithDelegate(m_impl->nnapiDelegate) != kTfLiteOk) {
                delete m_impl->nnapiDelegate;
                m_impl->nnapiDelegate = nullptr;
            }
            break;
        }
#endif
        case TfliteInferenceEngine::DelegateHint::XNNPACK: {
            auto opts = TfLiteXNNPackDelegateOptionsDefault();
            opts.num_threads = 2;
            m_impl->xnnpackDelegate = TfLiteXNNPackDelegateCreate(&opts);
            if (m_impl->xnnpackDelegate &&
                m_impl->interpreter->ModifyGraphWithDelegate(m_impl->xnnpackDelegate) != kTfLiteOk) {
                TfLiteXNNPackDelegateDelete(m_impl->xnnpackDelegate);
                m_impl->xnnpackDelegate = nullptr;
            }
            break;
        }
        default: break;
    }

    if (m_impl->interpreter->AllocateTensors() != kTfLiteOk) {
        m_lastError = "HandLandmarkDetector: AllocateTensors failed";
        return false;
    }
    m_impl->built = true;
    return true;
}
#endif

} // namespace ai
} // namespace video
} // namespace sdk
