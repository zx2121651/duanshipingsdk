#pragma once
/**
 * BodyPoseDetector.h
 *
 * 人体姿态估计 — 17 点关键点（兼容 MoveNet / BlazePose Lightning 输出格式）。
 *
 * 关键点索引（COCO 兼容）：
 *   0  nose          1  left_eye       2  right_eye
 *   3  left_ear      4  right_ear      5  left_shoulder
 *   6  right_shoulder 7 left_elbow     8  right_elbow
 *   9  left_wrist   10  right_wrist   11  left_hip
 *  12  right_hip    13  left_knee     14  right_knee
 *  15  left_ankle   16  right_ankle
 *
 * 坐标系：归一化 [0,1]，左上角 (0,0)，与 FaceLandmarkDetector 一致。
 *
 * 推理架构：与 FaceLandmarkDetector 相同的双线程异步设计：
 *   - 后台线程 (DetectThread) 以 1/3 渲染帧率运行 TFLite 推理
 *   - 渲染线程通过 getLatestResult() 获取最新结果（非阻塞）
 *
 * HAS_TFLITE 未定义时：退化为全零 stub（保证链接成功，供 CI 使用）。
 */

#include "../GLTypes.h"
#include <array>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <memory>
#include <cstdint>
#include <string>
#include <cmath>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// 关键点常量
// ---------------------------------------------------------------------------
static constexpr int kPoseKeypointCount = 17;

enum PoseKeypointIndex {
    POSE_NOSE          = 0,
    POSE_LEFT_EYE      = 1,
    POSE_RIGHT_EYE     = 2,
    POSE_LEFT_EAR      = 3,
    POSE_RIGHT_EAR     = 4,
    POSE_LEFT_SHOULDER = 5,
    POSE_RIGHT_SHOULDER= 6,
    POSE_LEFT_ELBOW    = 7,
    POSE_RIGHT_ELBOW   = 8,
    POSE_LEFT_WRIST    = 9,
    POSE_RIGHT_WRIST   = 10,
    POSE_LEFT_HIP      = 11,
    POSE_RIGHT_HIP     = 12,
    POSE_LEFT_KNEE     = 13,
    POSE_RIGHT_KNEE    = 14,
    POSE_LEFT_ANKLE    = 15,
    POSE_RIGHT_ANKLE   = 16
};

struct PoseKeypoint {
    float x     = 0.f;  ///< 归一化横坐标 [0,1]
    float y     = 0.f;  ///< 归一化纵坐标 [0,1]
    float score = 0.f;  ///< 置信度 [0,1]

    bool isValid(float minScore = 0.3f) const { return score >= minScore; }
};

struct PoseResult {
    bool detected   = false;
    float poseScore = 0.f;  ///< 整体姿态置信度（各关键点 score 均值）

    std::array<PoseKeypoint, kPoseKeypointCount> keypoints{};

    // ── 常用语义访问器 ──────────────────────────────────────────────────
    const PoseKeypoint& nose()           const { return keypoints[POSE_NOSE]; }
    const PoseKeypoint& leftShoulder()   const { return keypoints[POSE_LEFT_SHOULDER]; }
    const PoseKeypoint& rightShoulder()  const { return keypoints[POSE_RIGHT_SHOULDER]; }
    const PoseKeypoint& leftElbow()      const { return keypoints[POSE_LEFT_ELBOW]; }
    const PoseKeypoint& rightElbow()     const { return keypoints[POSE_RIGHT_ELBOW]; }
    const PoseKeypoint& leftWrist()      const { return keypoints[POSE_LEFT_WRIST]; }
    const PoseKeypoint& rightWrist()     const { return keypoints[POSE_RIGHT_WRIST]; }
    const PoseKeypoint& leftHip()        const { return keypoints[POSE_LEFT_HIP]; }
    const PoseKeypoint& rightHip()       const { return keypoints[POSE_RIGHT_HIP]; }

    /** 肩膀中心 */
    PoseKeypoint shoulderCenter() const {
        return { (leftShoulder().x  + rightShoulder().x)  * 0.5f,
                 (leftShoulder().y  + rightShoulder().y)  * 0.5f,
                 (leftShoulder().score + rightShoulder().score) * 0.5f };
    }
    /** 躯干高度估计（肩中到臀中距离） */
    float torsoHeight() const {
        auto sc = shoulderCenter();
        float hx = (leftHip().x + rightHip().x) * 0.5f;
        float hy = (leftHip().y + rightHip().y) * 0.5f;
        float dy = hy - sc.y, dx = hx - sc.x;
        return std::sqrt(dx * dx + dy * dy);
    }
};

struct PoseFrameResult {
    PoseResult pose;
    int64_t    frameTimestampNs = 0;
    float      inferenceTimeMs  = 0.f;
};

// ---------------------------------------------------------------------------
// BodyPoseDetector
// ---------------------------------------------------------------------------
class BodyPoseDetector {
public:
    BodyPoseDetector();
    ~BodyPoseDetector();

    BodyPoseDetector(const BodyPoseDetector&) = delete;
    BodyPoseDetector& operator=(const BodyPoseDetector&) = delete;

    // ---- 初始化 ----

    /**
     * 加载 TFLite 人体姿态模型（MoveNet Lightning 192×192 推荐）。
     * @param modelPath  .tflite 文件的本地路径
     */
    bool loadModel(const std::string& modelPath);
    bool loadModelFromBuffer(const void* data, size_t size);
    bool isLoaded() const { return m_loaded; }
    void release();

    // ---- 推理 ----

    /**
     * 提交一帧进行异步推理（非阻塞）。
     * @param rgbaPixels  RGBA 字节数组，大小 = w × h × 4
     * @param width / height  图像尺寸
     * @param timestampNs     帧时间戳（纳秒）
     */
    void submitFrame(const uint8_t* rgbaPixels, int width, int height,
                     int64_t timestampNs);

    /** 同步推理（低帧率场景用）。 */
    PoseFrameResult runSync(const uint8_t* rgbaPixels, int width, int height);

    /** 获取最新推理结果（线程安全，渲染线程调用）。 */
    PoseFrameResult getLatestResult() const;

    /** 推理完成回调（可选，从后台线程调用）。 */
    void setResultCallback(std::function<void(const PoseFrameResult&)> cb);

    float getAverageInferenceMs() const { return m_avgInferenceMs; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_loaded = false;

    mutable std::mutex  m_resultMutex;
    PoseFrameResult     m_latestResult;

    std::function<void(const PoseFrameResult&)> m_callback;

    // ── 后台线程 ────────────────────────────────────────────────────────
    std::thread       m_detectThread;
    std::atomic<bool> m_stopThread{false};
    std::atomic<bool> m_framePending{false};

    struct PendingFrame {
        std::vector<uint8_t> pixels;
        int width = 0, height = 0;
        int64_t timestampNs = 0;
    };
    mutable std::mutex m_frameMutex;
    PendingFrame       m_pendingFrame;

    float m_avgInferenceMs = 0.f;
    int   m_inferenceCount = 0;

    void startDetectThread();
    void detectLoop();
    PoseFrameResult runInferenceInternal(const PendingFrame& frame);
    PoseFrameResult runTFLite(const uint8_t* rgba, int w, int h, int64_t ts);

    /** Decode raw model output (shape [1,1,17,3]) → PoseResult */
    static PoseResult decodeKeypoints(const float* output, int w, int h);

#ifdef HAS_TFLITE
    bool buildInterpreterInternal();
#endif
};

} // namespace ai
} // namespace video
} // namespace sdk
