#pragma once
/**
 * GestureRecognizer.h
 *
 * 基于人体姿态关键点的规则手势识别器（无需额外 TFLite 模型）。
 *
 * 支持的手势：
 *   ThumbsUp    — 手腕高于肘，肘高于肩（任意一侧）
 *   Wave        — 手腕高于鼻部且在最近 N 帧内水平位移超过阈值
 *   HeartHands  — 双手腕相互靠近且均高于肩膀（爱心比心）
 *   Victory     — 手腕高于头顶（双手举起，适合胜利/欢呼）
 *   HandRaise   — 单手腕高于同侧肩膀（举手）
 *
 * 时序平滑：所有手势需连续 kStableFrames 帧满足条件才输出，
 * 防止单帧误识别。
 *
 * 用法：
 *   GestureRecognizer recognizer;
 *   recognizer.update(poseResult);
 *   GestureType g = recognizer.currentGesture();
 *   if (g == GestureType::ThumbsUp) { ... }
 */

#include "BodyPoseDetector.h"
#include <cstdint>
#include <array>
#include <functional>
#include <string>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// Gesture types
// ---------------------------------------------------------------------------
enum class GestureType {
    None       = 0,
    ThumbsUp   = 1,  ///< 大拇指朝上（手腕-肘-肩竖直排列）
    Wave       = 2,  ///< 挥手（手腕高于鼻子且有水平运动）
    HeartHands = 3,  ///< 比心（双腕靠近且高于肩）
    Victory    = 4,  ///< 双手举起（双腕高于头顶）
    HandRaise  = 5,  ///< 举手（单腕高于同侧肩）
};

inline const char* gestureName(GestureType g) {
    switch (g) {
        case GestureType::ThumbsUp:   return "thumbs_up";
        case GestureType::Wave:       return "wave";
        case GestureType::HeartHands: return "heart_hands";
        case GestureType::Victory:    return "victory";
        case GestureType::HandRaise:  return "hand_raise";
        default:                      return "none";
    }
}

inline GestureType gestureFromName(const std::string& name) {
    if (name == "thumbs_up")   return GestureType::ThumbsUp;
    if (name == "wave")        return GestureType::Wave;
    if (name == "heart_hands") return GestureType::HeartHands;
    if (name == "victory")     return GestureType::Victory;
    if (name == "hand_raise")  return GestureType::HandRaise;
    return GestureType::None;
}

// ---------------------------------------------------------------------------
// GestureRecognizer
// ---------------------------------------------------------------------------
class GestureRecognizer {
public:
    /** 手势需连续稳定帧数才输出（默认 3 帧，约 100ms @30fps）。*/
    explicit GestureRecognizer(int stableFrames = 3);

    /**
     * 用每帧姿态结果更新识别器状态。
     * 应在渲染线程每帧调用一次。
     * @return 当前稳定手势（可能与上帧相同）
     */
    GestureType update(const PoseResult& pose);

    /** 当前已稳定的手势。 */
    GestureType currentGesture() const { return m_stableGesture; }

    /** 当手势状态改变时触发（从 None→X 或 X→None 或 X→Y）。 */
    void setOnGestureChanged(std::function<void(GestureType)> cb) {
        m_callback = std::move(cb);
    }

    /** 复位所有状态。 */
    void reset();

    /** 调整稳定帧数阈值（低延迟场景可设为 1）。 */
    void setStableFrames(int n) { m_stableFrames = n > 0 ? n : 1; }

private:
    int         m_stableFrames;
    GestureType m_stableGesture   = GestureType::None;
    GestureType m_candidateGesture = GestureType::None;
    int         m_candidateCount   = 0;

    std::function<void(GestureType)> m_callback;

    // ── 挥手历史（需要时序信息）───────────────────────────────────────
    static constexpr int kWaveHistoryLen = 8;
    std::array<float, kWaveHistoryLen> m_wristXHistory{};
    int   m_wristHistoryIdx  = 0;
    bool  m_wristHistoryFull = false;

    // ── 单帧规则检测 ─────────────────────────────────────────────────
    GestureType detectInstant(const PoseResult& pose);

    bool detectThumbsUp   (const PoseResult& p) const;
    bool detectHeartHands (const PoseResult& p) const;
    bool detectVictory    (const PoseResult& p) const;
    bool detectHandRaise  (const PoseResult& p) const;
    bool detectWave       (const PoseResult& p);

    /** 两点欧氏距离（归一化空间）。 */
    static float dist(const PoseKeypoint& a, const PoseKeypoint& b);
};

} // namespace ai
} // namespace video
} // namespace sdk
