/**
 * FaceLandmarkDetector.cpp
 *
 * 人脸关键点检测实现。
 * HAS_TFLITE  — TensorFlow Lite 实际推理
 * 否则        — 返回可预测的 stub 结果（测试/无 GPU 环境可用）
 */

#include "../../include/ai/FaceLandmarkDetector.h"

#define LOG_TAG "FaceLandmarkDetector"
#include "../../include/Log.h"

#include <chrono>
#include <cmath>
#include <cstring>

#ifdef HAS_TFLITE
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"
#ifdef HAS_TFLITE_GPU_DELEGATE
#include "tensorflow/lite/delegates/gpu/delegate.h"
#endif
#endif

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// Impl (TFLite 推理持有者)
// ---------------------------------------------------------------------------
struct FaceLandmarkDetector::Impl {
#ifdef HAS_TFLITE
    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter>     interpreter;
    std::vector<uint8_t>                     modelBuffer; // loadModelFromBuffer 缓冲
    int inputW  = 192;
    int inputH  = 192;
    int inputTensorIdx  = -1;
    int outputTensorIdx = -1; // [1, 212] = 106 landmarks × 2
#endif
    bool ready = false;
};

// ---------------------------------------------------------------------------
FaceLandmarkDetector::FaceLandmarkDetector()
    : m_impl(std::make_unique<Impl>()) {}

FaceLandmarkDetector::~FaceLandmarkDetector() {
    release();
}

void FaceLandmarkDetector::release() {
    m_stopThread.store(true);
    m_framePending.store(true); // 唤醒检测线程
    if (m_detectThread.joinable()) m_detectThread.join();
    m_loaded = false;
}

// ---------------------------------------------------------------------------
// 模型加载
// ---------------------------------------------------------------------------
bool FaceLandmarkDetector::loadModel(const std::string& modelPath) {
#ifdef HAS_TFLITE
    m_impl->model = tflite::FlatBufferModel::BuildFromFile(modelPath.c_str());
    if (!m_impl->model) {
        LOGE("FaceLandmarkDetector: failed to load model: %s", modelPath.c_str());
        return false;
    }
    return buildInterpreterInternal();
#else
    LOGI("FaceLandmarkDetector: stub mode (HAS_TFLITE not set), model=%s", modelPath.c_str());
    m_impl->ready = true;
    m_loaded = true;
    startDetectThread();
    return true;
#endif
}

bool FaceLandmarkDetector::loadModelFromBuffer(const void* data, size_t size) {
#ifdef HAS_TFLITE
    m_impl->modelBuffer.assign(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + size);
    m_impl->model = tflite::FlatBufferModel::BuildFromBuffer(
        reinterpret_cast<const char*>(m_impl->modelBuffer.data()),
        m_impl->modelBuffer.size());
    if (!m_impl->model) {
        LOGE("FaceLandmarkDetector: failed to build model from buffer");
        return false;
    }
    return buildInterpreterInternal();
#else
    (void)data; (void)size;
    m_impl->ready = true;
    m_loaded = true;
    startDetectThread();
    return true;
#endif
}

#ifdef HAS_TFLITE
bool FaceLandmarkDetector::buildInterpreterInternal() {
    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*m_impl->model, resolver);
    builder(&m_impl->interpreter);
    if (!m_impl->interpreter) return false;

#ifdef HAS_TFLITE_GPU_DELEGATE
    TfLiteGpuDelegateOptionsV2 opts = TfLiteGpuDelegateOptionsV2Default();
    opts.inference_preference = TFLITE_GPU_INFERENCE_PREFERENCE_FAST_SINGLE_ANSWER;
    auto* delegate = TfLiteGpuDelegateV2Create(&opts);
    if (m_impl->interpreter->ModifyGraphWithDelegate(delegate) != kTfLiteOk)
        LOGW("FaceLandmarkDetector: GPU delegate failed, falling back to CPU");
#endif

    m_impl->interpreter->SetNumThreads(2);
    if (m_impl->interpreter->AllocateTensors() != kTfLiteOk) {
        LOGE("FaceLandmarkDetector: AllocateTensors failed");
        return false;
    }

    auto* inputTensor  = m_impl->interpreter->input_tensor(0);
    m_impl->inputW     = inputTensor->dims->data[2];
    m_impl->inputH     = inputTensor->dims->data[1];
    m_impl->inputTensorIdx  = m_impl->interpreter->inputs()[0];
    m_impl->outputTensorIdx = m_impl->interpreter->outputs()[0];

    LOGI("FaceLandmarkDetector: model input %dx%d", m_impl->inputW, m_impl->inputH);
    m_impl->ready = true;
    m_loaded = true;
    startDetectThread();
    return true;
}
#endif

void FaceLandmarkDetector::startDetectThread() {
    m_stopThread.store(false);
    m_detectThread = std::thread(&FaceLandmarkDetector::detectLoop, this);
}

// ---------------------------------------------------------------------------
// 后台检测线程
// ---------------------------------------------------------------------------
void FaceLandmarkDetector::detectLoop() {
    while (!m_stopThread.load()) {
        // 等待新帧
        while (!m_framePending.load() && !m_stopThread.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (m_stopThread.load()) break;
        m_framePending.store(false);

        PendingFrame frame;
        {
            std::lock_guard<std::mutex> lk(m_frameMutex);
            frame = m_pendingFrame;
        }
        if (frame.pixels.empty()) continue;

        auto result = runInferenceInternal(frame);

        {
            std::lock_guard<std::mutex> lk(m_resultMutex);
            m_latestResult = result;
        }
        if (m_callback) m_callback(result);
    }
}

// ---------------------------------------------------------------------------
// 提交帧
// ---------------------------------------------------------------------------
void FaceLandmarkDetector::submitFrame(const uint8_t* rgba, int w, int h, int64_t ts) {
    {
        std::lock_guard<std::mutex> lk(m_frameMutex);
        m_pendingFrame.pixels.assign(rgba, rgba + w * h * 4);
        m_pendingFrame.width       = w;
        m_pendingFrame.height      = h;
        m_pendingFrame.timestampNs = ts;
    }
    m_framePending.store(true);
}

// ---------------------------------------------------------------------------
// 同步推理
// ---------------------------------------------------------------------------
LandmarkFrameResult FaceLandmarkDetector::runSync(const uint8_t* rgba, int w, int h) {
    PendingFrame frame;
    frame.pixels.assign(rgba, rgba + w * h * 4);
    frame.width  = w;
    frame.height = h;
    frame.timestampNs = 0;
    return runInferenceInternal(frame);
}

LandmarkFrameResult FaceLandmarkDetector::getLatestResult() const {
    std::lock_guard<std::mutex> lk(m_resultMutex);
    return m_latestResult;
}

void FaceLandmarkDetector::setResultCallback(
    std::function<void(const LandmarkFrameResult&)> cb) {
    m_callback = std::move(cb);
}

float FaceLandmarkDetector::getAverageInferenceMs() const { return m_avgInferenceMs; }

// ---------------------------------------------------------------------------
// 核心推理分发
// ---------------------------------------------------------------------------
LandmarkFrameResult FaceLandmarkDetector::runInferenceInternal(const PendingFrame& frame) {
    auto t0 = std::chrono::steady_clock::now();
    auto res = runTFLite(frame.pixels.data(), frame.width, frame.height,
                         frame.timestampNs);
    auto t1 = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    res.inferenceTimeMs = ms;

    // 滑动平均
    ++m_inferenceCount;
    m_avgInferenceMs += (ms - m_avgInferenceMs) / m_inferenceCount;
    return res;
}

// ---------------------------------------------------------------------------
// TFLite 推理 / stub
// ---------------------------------------------------------------------------
LandmarkFrameResult FaceLandmarkDetector::runTFLite(
    const uint8_t* rgba, int w, int h, int64_t ts)
{
    LandmarkFrameResult result;
    result.frameTimestampNs = ts;

#ifdef HAS_TFLITE
    if (!m_impl->ready || !m_impl->interpreter) {
        return result;
    }

    // 1. Resize RGBA → model input size (bilinear)
    int mw = m_impl->inputW, mh = m_impl->inputH;
    std::vector<uint8_t> resized(mw * mh * 3);
    for (int y = 0; y < mh; ++y) {
        for (int x = 0; x < mw; ++x) {
            int sx = x * w / mw, sy = y * h / mh;
            const uint8_t* p = rgba + (sy * w + sx) * 4;
            resized[(y * mw + x) * 3 + 0] = p[0];
            resized[(y * mw + x) * 3 + 1] = p[1];
            resized[(y * mw + x) * 3 + 2] = p[2];
        }
    }

    // 2. 填充输入 tensor（float 归一化 or uint8）
    auto* inputTensor = m_impl->interpreter->tensor(m_impl->inputTensorIdx);
    if (inputTensor->type == kTfLiteFloat32) {
        float* inp = m_impl->interpreter->typed_input_tensor<float>(0);
        for (int i = 0; i < mw * mh * 3; ++i)
            inp[i] = resized[i] / 255.0f;
    } else {
        uint8_t* inp = m_impl->interpreter->typed_input_tensor<uint8_t>(0);
        std::memcpy(inp, resized.data(), resized.size());
    }

    // 3. Invoke
    if (m_impl->interpreter->Invoke() != kTfLiteOk) return result;

    // 4. 解码输出
    float* out = m_impl->interpreter->typed_output_tensor<float>(0);
    auto* outTensor = m_impl->interpreter->tensor(m_impl->outputTensorIdx);
    int outLen = 1;
    for (int d = 0; d < outTensor->dims->size; ++d)
        outLen *= outTensor->dims->data[d];

    if (outLen >= kFaceLandmarkCount * 2) {
        result.faceCount = 1;
        result.faces[0].detected = true;
        result.faces[0].faceScore = 1.0f;
        decodeLandmarks(out, outLen, w, h, result.faces[0]);
    }
#else
    // Stub: 生成中央人脸模拟数据（用于测试/无模型环境）
    result.faceCount = 1;
    auto& face = result.faces[0];
    face.detected   = true;
    face.faceScore  = 0.95f;
    face.boundingBox[0] = 0.25f; face.boundingBox[1] = 0.15f;
    face.boundingBox[2] = 0.50f; face.boundingBox[3] = 0.70f;

    // 生成近似人脸关键点（中心帧，用于开发期视觉验证）
    const float cx = 0.5f, cy = 0.5f;
    // 脸部轮廓 17 点（椭圆分布）
    for (int i = 0; i < 17; ++i) {
        float angle = static_cast<float>(i) / 16.f * 3.14159f; // 0..pi
        face.landmarks[i].x = cx + 0.18f * std::cos(3.14159f - angle);
        face.landmarks[i].y = cy + 0.26f * std::sin(angle);
        face.landmarks[i].score = 0.9f;
    }
    // 眉毛 [17-26]
    for (int i = 17; i < 27; ++i) {
        face.landmarks[i].x = cx + (i < 22 ? 0.07f : -0.07f) + (i % 5 - 2) * 0.02f;
        face.landmarks[i].y = cy - 0.12f;
        face.landmarks[i].score = 0.85f;
    }
    // 鼻梁 [27-35]
    for (int i = 27; i < 36; ++i) {
        face.landmarks[i].x = cx;
        face.landmarks[i].y = cy - 0.10f + (i - 27) * 0.025f;
        face.landmarks[i].score = 0.9f;
    }
    // 眼睛 [36-47]
    for (int i = 36; i < 42; ++i) { // 右眼
        float a = static_cast<float>(i - 36) / 6.f * 2.f * 3.14159f;
        face.landmarks[i].x = cx + 0.08f + 0.035f * std::cos(a);
        face.landmarks[i].y = cy - 0.075f + 0.015f * std::sin(a);
        face.landmarks[i].score = 0.9f;
    }
    for (int i = 42; i < 48; ++i) { // 左眼
        float a = static_cast<float>(i - 42) / 6.f * 2.f * 3.14159f;
        face.landmarks[i].x = cx - 0.08f + 0.035f * std::cos(a);
        face.landmarks[i].y = cy - 0.075f + 0.015f * std::sin(a);
        face.landmarks[i].score = 0.9f;
    }
    // 嘴部 [48-67]
    for (int i = 48; i < 68; ++i) {
        float a = static_cast<float>(i - 48) / 20.f * 2.f * 3.14159f;
        face.landmarks[i].x = cx + 0.07f * std::cos(a);
        face.landmarks[i].y = cy + 0.09f + 0.025f * std::sin(a);
        face.landmarks[i].score = 0.88f;
    }
    // 填充点 [68-105]
    for (int i = 68; i < kFaceLandmarkCount; ++i) {
        face.landmarks[i].x = cx + (i % 7 - 3) * 0.03f;
        face.landmarks[i].y = cy + (i % 5 - 2) * 0.04f;
        face.landmarks[i].score = 0.75f;
    }
#endif
    return result;
}

// ---------------------------------------------------------------------------
// 坐标解码（TFLite 输出 → 归一化 FaceLandmark）
// ---------------------------------------------------------------------------
void FaceLandmarkDetector::decodeLandmarks(const float* out, int outLen,
                                            int imgW, int imgH, FaceResult& face)
{
    (void)imgW; (void)imgH; (void)outLen;
    for (int i = 0; i < kFaceLandmarkCount; ++i) {
        face.landmarks[i].x     = out[i * 2 + 0]; // already normalized [0,1]
        face.landmarks[i].y     = out[i * 2 + 1];
        face.landmarks[i].score = (outLen > kFaceLandmarkCount * 2) ? out[kFaceLandmarkCount * 2 + i] : 1.0f;
    }
    face.detected   = true;
    face.faceScore  = 1.0f;
}

} // namespace ai
} // namespace video
} // namespace sdk
