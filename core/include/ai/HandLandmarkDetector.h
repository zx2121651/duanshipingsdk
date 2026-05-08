#pragma once
/**
 * HandLandmarkDetector.h
 *
 * 实时手部 21 关键点检测（MediaPipe Hands 兼容格式）。
 *
 * 关键点布局（每只手 21 点，归一化 [0,1]）：
 *   0        WRIST
 *   1-4      THUMB   (CMC, MCP, IP, TIP)
 *   5-8      INDEX   (MCP, PIP, DIP, TIP)
 *   9-12     MIDDLE  (MCP, PIP, DIP, TIP)
 *   13-16    RING    (MCP, PIP, DIP, TIP)
 *   17-20    PINKY   (MCP, PIP, DIP, TIP)
 *
 * 架构（与 FaceLandmarkDetector 保持一致）：
 *   - 后台推理线程以降频运行 TFLite 模型
 *   - 渲染线程调用 getLatestResult() 获取最新结果（无阻塞）
 *   - 支持最多 2 只手同时追踪
 *   - HAS_TFLITE 未定义时退化为全零 stub（不崩溃）
 *
 * 用法：
 *   HandLandmarkDetector detector;
 *   detector.loadModel("/data/.../hand_landmark.tflite");
 *   detector.submitFrame(rgbaPixels, w, h, tsNs);
 *   auto result = detector.getLatestResult();
 *   if (result.handCount > 0) {
 *       auto tip = result.hands[0].landmarks[HandLandmarkDetector::INDEX_TIP];
 *   }
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
// 常量 & 关键点语义索引
// ---------------------------------------------------------------------------
static constexpr int kHandLandmarkCount = 21;
static constexpr int kMaxHands          = 2;

// 语义索引（方便代码可读性）
struct HandLandmarkIndex {
    static constexpr int WRIST       = 0;
    static constexpr int THUMB_CMC   = 1;
    static constexpr int THUMB_MCP   = 2;
    static constexpr int THUMB_IP    = 3;
    static constexpr int THUMB_TIP   = 4;
    static constexpr int INDEX_MCP   = 5;
    static constexpr int INDEX_PIP   = 6;
    static constexpr int INDEX_DIP   = 7;
    static constexpr int INDEX_TIP   = 8;
    static constexpr int MIDDLE_MCP  = 9;
    static constexpr int MIDDLE_PIP  = 10;
    static constexpr int MIDDLE_DIP  = 11;
    static constexpr int MIDDLE_TIP  = 12;
    static constexpr int RING_MCP    = 13;
    static constexpr int RING_PIP    = 14;
    static constexpr int RING_DIP    = 15;
    static constexpr int RING_TIP    = 16;
    static constexpr int PINKY_MCP   = 17;
    static constexpr int PINKY_PIP   = 18;
    static constexpr int PINKY_DIP   = 19;
    static constexpr int PINKY_TIP   = 20;
};

// ---------------------------------------------------------------------------
// 单关键点
// ---------------------------------------------------------------------------
struct HandLandmark {
    float x     = 0.f;  ///< 归一化横坐标 [0,1]，左上角为原点
    float y     = 0.f;  ///< 归一化纵坐标 [0,1]
    float z     = 0.f;  ///< 相对深度（相对于手腕）；越负越靠近摄像头
    float score = 0.f;  ///< 置信度 [0,1]
};

// ---------------------------------------------------------------------------
// 单只手的检测结果
// ---------------------------------------------------------------------------
struct HandResult {
    bool detected    = false;
    bool isRightHand = true;        ///< true = 右手；false = 左手（镜像图像）
    float handScore  = 0.f;         ///< 整体手部置信度

    std::array<HandLandmark, kHandLandmarkCount> landmarks{};

    // ── 常用语义访问器 ─────────────────────────────────────────────────────
    const HandLandmark& wrist()      const { return landmarks[HandLandmarkIndex::WRIST]; }
    const HandLandmark& thumbTip()   const { return landmarks[HandLandmarkIndex::THUMB_TIP]; }
    const HandLandmark& indexTip()   const { return landmarks[HandLandmarkIndex::INDEX_TIP]; }
    const HandLandmark& middleTip()  const { return landmarks[HandLandmarkIndex::MIDDLE_TIP]; }
    const HandLandmark& ringTip()    const { return landmarks[HandLandmarkIndex::RING_TIP]; }
    const HandLandmark& pinkyTip()   const { return landmarks[HandLandmarkIndex::PINKY_TIP]; }

    /**
     * 判断某根手指是否伸展（指尖 y 坐标显著小于 MCP 节点 y 坐标）。
     * @param mcpIdx   MCP 关键点索引（如 INDEX_MCP = 5）
     * @param tipIdx   TIP 关键点索引（如 INDEX_TIP = 8）
     * @param thresh   阈值，默认 0.05（归一化坐标）
     */
    bool isFingerExtended(int mcpIdx, int tipIdx, float thresh = 0.05f) const {
        return (landmarks[mcpIdx].y - landmarks[tipIdx].y) > thresh;
    }

    /** 快捷：是否比 "耶" 手势（食指+中指伸展，其余弯曲） */
    bool isVictoryGesture() const {
        return isFingerExtended(HandLandmarkIndex::INDEX_MCP,  HandLandmarkIndex::INDEX_TIP)
            && isFingerExtended(HandLandmarkIndex::MIDDLE_MCP, HandLandmarkIndex::MIDDLE_TIP)
            && !isFingerExtended(HandLandmarkIndex::RING_MCP,  HandLandmarkIndex::RING_TIP)
            && !isFingerExtended(HandLandmarkIndex::PINKY_MCP, HandLandmarkIndex::PINKY_TIP);
    }

    /** 快捷：是否 "OK" 手势（拇指+食指接近，其余伸展） */
    bool isOkGesture(float pinchThresh = 0.06f) const {
        float dx = thumbTip().x - indexTip().x;
        float dy = thumbTip().y - indexTip().y;
        float dist = dx * dx + dy * dy;
        return dist < (pinchThresh * pinchThresh)
            && isFingerExtended(HandLandmarkIndex::MIDDLE_MCP, HandLandmarkIndex::MIDDLE_TIP);
    }

    /** 快捷：是否捏合（拇指+食指指尖距离 < threshold） */
    bool isPinching(float thresh = 0.06f) const {
        float dx = thumbTip().x - indexTip().x;
        float dy = thumbTip().y - indexTip().y;
        return (dx * dx + dy * dy) < (thresh * thresh);
    }
};

// ---------------------------------------------------------------------------
// 帧级别的检测结果（包含最多 kMaxHands 只手）
// ---------------------------------------------------------------------------
struct HandFrameResult {
    int  handCount       = 0;
    std::array<HandResult, kMaxHands> hands{};
    int64_t frameTimestampNs = 0;
    float   inferenceTimeMs  = 0.f;
};

// ---------------------------------------------------------------------------
// HandLandmarkDetector
// ---------------------------------------------------------------------------
class HandLandmarkDetector {
public:
    HandLandmarkDetector();
    ~HandLandmarkDetector();

    HandLandmarkDetector(const HandLandmarkDetector&) = delete;
    HandLandmarkDetector& operator=(const HandLandmarkDetector&) = delete;

    // ── 初始化 ─────────────────────────────────────────────────────────────

    /**
     * 从文件路径加载 hand_landmark.tflite 模型。
     * @return true = 成功；false = 失败（查看 getLastError()）
     */
    bool loadModel(const std::string& modelPath);

    /** 从内存 buffer 加载模型（适合从 Android assets 直接读入）。 */
    bool loadModelFromBuffer(const void* data, size_t size);

    bool isLoaded() const { return m_loaded; }

    /** 设置 TFLite delegate 偏好；必须在 loadModel() 之前调用。 */
    void setDelegateHint(TfliteInferenceEngine::DelegateHint hint) {
        m_delegateHint = hint;
    }
    TfliteInferenceEngine::DelegateHint getDelegateHint() const {
        return m_delegateHint;
    }

    void release();

    // ── 推理 ───────────────────────────────────────────────────────────────

    /**
     * 提交一帧进行推理（非阻塞，后台线程）。
     * @param rgbaPixels  RGBA 字节数组，大小 = w × h × 4
     */
    void submitFrame(const uint8_t* rgbaPixels, int width, int height,
                     int64_t timestampNs);

    /**
     * 同步推理（阻塞直到完成）。适用于低帧率导出场景。
     */
    HandFrameResult runSync(const uint8_t* rgbaPixels, int width, int height);

    /**
     * 获取最新推理结果（线程安全）。
     * 若后台线程尚未完成，返回上一帧结果（帧间连续）。
     */
    HandFrameResult getLatestResult() const;

    /** 设置推理完成回调（从后台线程调用，注意线程安全）。 */
    void setResultCallback(std::function<void(const HandFrameResult&)> cb);

    /** 最近一次操作的错误描述 */
    const std::string& getLastError() const { return m_lastError; }

    /** 过去 N 帧平均推理耗时（ms） */
    float getAverageInferenceMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_loaded = false;
    std::string m_lastError;
    TfliteInferenceEngine::DelegateHint m_delegateHint
        = TfliteInferenceEngine::DelegateHint::GPU;

    mutable std::mutex  m_resultMutex;
    HandFrameResult     m_latestResult;

    std::function<void(const HandFrameResult&)> m_callback;

    std::thread       m_detectThread;
    std::atomic<bool> m_stopThread{false};
    std::atomic<bool> m_framePending{false};

    struct PendingFrame {
        std::vector<uint8_t> pixels;
        int     width       = 0;
        int     height      = 0;
        int64_t timestampNs = 0;
    };
    mutable std::mutex m_frameMutex;
    PendingFrame       m_pendingFrame;

    float m_avgInferenceMs = 0.f;
    int   m_inferenceCount = 0;

    void startDetectThread();
    void detectLoop();
    HandFrameResult runInferenceInternal(const PendingFrame& frame);
    HandFrameResult runTFLite(const uint8_t* rgba, int w, int h, int64_t ts);

    // 后处理：将模型输出 float[21×3] 解码为 HandResult
    static HandResult decodeHandResult(const float* coords, int numCoords,
                                       float handScore, bool isRight);
};

} // namespace ai
} // namespace video
} // namespace sdk
