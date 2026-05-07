#pragma once
/**
 * FaceLandmarkDetector.h
 *
 * 实时人脸关键点检测 — 对标抖音 106 点人脸 AI。
 *
 * 架构：
 *   1. 检测线程（DetectThread）以 1/3 渲染帧率运行 TFLite 推理
 *   2. 渲染线程从缓存读取最新 LandmarkResult，无需等待推理完成
 *   3. 支持最多 MAX_FACES 张人脸同时追踪
 *
 * 关键点布局（106 点，兼容抖音 SDK 坐标系）：
 *   [0-16]   脸部轮廓（17 点）
 *   [17-21]  右眉（5 点）
 *   [22-26]  左眉（5 点）
 *   [27-35]  鼻梁（9 点）
 *   [36-41]  右眼（6 点）
 *   [42-47]  左眼（6 点）
 *   [48-67]  嘴部（20 点）
 *   [68-105] 面部填充点（38 点）
 *
 * 坐标系：归一化 [0,1]，左上角 (0,0)，与 GL 纹理坐标一致。
 */

#include "../GLTypes.h"
#include "TfliteInferenceEngine.h"
#include <array>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <memory>
#include <cstdint>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// 单张人脸的关键点结果
// ---------------------------------------------------------------------------
static constexpr int kFaceLandmarkCount = 106;
static constexpr int kMaxFaces          = 2;

struct FaceLandmark {
    float x = 0.f;  ///< 归一化横坐标 [0,1]
    float y = 0.f;  ///< 归一化纵坐标 [0,1]
    float score = 0.f; ///< 置信度 [0,1]
};

struct FaceResult {
    bool  detected = false;
    float faceScore = 0.f;          ///< 整体人脸置信度
    float boundingBox[4] = {};      ///< [x, y, w, h] 归一化

    std::array<FaceLandmark, kFaceLandmarkCount> landmarks{};

    // --------------- 常用语义访问器 ---------------
    /** 下颌中心点（点 8） */
    FaceLandmark chin()        const { return landmarks[8]; }
    /** 左脸颊（点 0） */
    FaceLandmark leftCheek()   const { return landmarks[0]; }
    /** 右脸颊（点 16） */
    FaceLandmark rightCheek()  const { return landmarks[16]; }
    /** 鼻尖（点 30） */
    FaceLandmark noseTip()     const { return landmarks[30]; }
    /** 左眼中心（点 39） */
    FaceLandmark leftEye()     const { return landmarks[39]; }
    /** 右眼中心（点 42） */
    FaceLandmark rightEye()    const { return landmarks[42]; }
    /** 嘴角左（点 48） */
    FaceLandmark mouthLeft()   const { return landmarks[48]; }
    /** 嘴角右（点 54） */
    FaceLandmark mouthRight()  const { return landmarks[54]; }
    /** 额头顶（点 27） */
    FaceLandmark forehead()    const { return landmarks[27]; }
};

struct LandmarkFrameResult {
    int    faceCount = 0;
    std::array<FaceResult, kMaxFaces> faces{};
    int64_t frameTimestampNs = 0;
    float   inferenceTimeMs  = 0.f;
};

// ---------------------------------------------------------------------------
// FaceLandmarkDetector
// ---------------------------------------------------------------------------
class FaceLandmarkDetector {
public:
    FaceLandmarkDetector();
    ~FaceLandmarkDetector();

    FaceLandmarkDetector(const FaceLandmarkDetector&) = delete;
    FaceLandmarkDetector& operator=(const FaceLandmarkDetector&) = delete;

    // ---- 初始化 / 销毁 ----

    /**
     * 加载人脸关键点 TFLite 模型。
     * @param modelPath  .tflite 文件路径（Android 侧由 AssetManager 解压后传入）
     * @return true = 成功
     */
    bool loadModel(const std::string& modelPath);
    bool loadModelFromBuffer(const void* data, size_t size);
    bool isLoaded() const { return m_loaded; }
    void release();

    /**
     * 设置 TFLite delegate 偏好（GPU/NNAPI/XNNPACK/CPU）。
     * 必须在 loadModel() 之前调用；默认 GPU（回退链：GPU→CPU）。
     * @param hint  TfliteInferenceEngine::DelegateHint 值（强转为 int 传入）
     */
    void setDelegateHint(sdk::video::ai::TfliteInferenceEngine::DelegateHint hint) {
        m_delegateHint = hint;
    }
    sdk::video::ai::TfliteInferenceEngine::DelegateHint getDelegateHint() const {
        return m_delegateHint;
    }

    // ---- 推理 ----

    /**
     * 提交一帧进行推理（非阻塞）。
     * 推理在内部后台线程执行；调用 getLatestResult() 获取最近结果。
     *
     * @param rgbaPixels  RGBA 字节数组，大小 = w × h × 4
     * @param width       图像宽度
     * @param height      图像高度
     * @param timestampNs 帧时间戳（纳秒）
     */
    void submitFrame(const uint8_t* rgbaPixels, int width, int height,
                     int64_t timestampNs);

    /**
     * 同步推理（渲染线程直接调用，会阻塞到推理完成）。
     * 适用于低帧率导出场景。
     */
    LandmarkFrameResult runSync(const uint8_t* rgbaPixels, int width, int height);

    /**
     * 获取最新推理结果（线程安全，渲染线程调用）。
     * 若尚未推理完成，返回上一帧结果（平滑连续）。
     */
    LandmarkFrameResult getLatestResult() const;

    /** 设置推理完成回调（可选，从后台线程调用） */
    void setResultCallback(std::function<void(const LandmarkFrameResult&)> cb);

    /** 统计：过去 N 帧平均推理耗时 */
    float getAverageInferenceMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_loaded = false;

    mutable std::mutex m_resultMutex;
    LandmarkFrameResult m_latestResult;

    std::function<void(const LandmarkFrameResult&)> m_callback;

    // 后台检测线程
    std::thread              m_detectThread;
    std::atomic<bool>        m_stopThread{false};
    std::atomic<bool>        m_framePending{false};

    // 待推理帧缓冲（双缓冲）
    struct PendingFrame {
        std::vector<uint8_t> pixels;
        int   width  = 0;
        int   height = 0;
        int64_t timestampNs = 0;
    };
    mutable std::mutex m_frameMutex;
    PendingFrame m_pendingFrame;

    // Pending delegate hint — applied in loadModel / buildInterpreterInternal
    TfliteInferenceEngine::DelegateHint m_delegateHint
        = TfliteInferenceEngine::DelegateHint::GPU;

    // 性能统计
    float m_avgInferenceMs = 0.f;
    int   m_inferenceCount = 0;

    void startDetectThread();
    void detectLoop();
    LandmarkFrameResult runInferenceInternal(const PendingFrame& frame);

    // TFLite 推理（HAS_TFLITE 时真实实现，否则返回 stub 结果）
    LandmarkFrameResult runTFLite(const uint8_t* rgba, int w, int h, int64_t ts);
#ifdef HAS_TFLITE
    bool buildInterpreterInternal();
#endif

    // 后处理：sigmoid + 坐标反归一化
    static void decodeLandmarks(const float* rawOutput, int outputLen,
                                int imgW, int imgH, FaceResult& out);
};

} // namespace ai
} // namespace video
} // namespace sdk
