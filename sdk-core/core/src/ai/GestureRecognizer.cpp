#ifndef LOG_TAG
#define LOG_TAG "GestureRecognizer"
#endif
#include "../../include/ai/GestureRecognizer.h"
#include "../../include/Log.h"
#include <cmath>
#include <algorithm>

namespace sdk {
namespace video {
namespace ai {

GestureRecognizer::GestureRecognizer(int stableFrames)
    : m_stableFrames(stableFrames > 0 ? stableFrames : 1) {
    m_wristXHistory.fill(0.f);
}

void GestureRecognizer::reset() {
    m_stableGesture    = GestureType::None;
    m_candidateGesture = GestureType::None;
    m_candidateCount   = 0;
    m_wristXHistory.fill(0.f);
    m_wristHistoryIdx  = 0;
    m_wristHistoryFull = false;
}

GestureType GestureRecognizer::update(const PoseResult& pose) {
    GestureType instant = detectInstant(pose);

    if (instant == m_candidateGesture) {
        ++m_candidateCount;
    } else {
        m_candidateGesture = instant;
        m_candidateCount   = 1;
    }

    GestureType next = (m_candidateCount >= m_stableFrames)
                       ? m_candidateGesture
                       : m_stableGesture; // hold until new stable

    if (next != m_stableGesture) {
        LOGD("Gesture: %s -> %s", gestureName(m_stableGesture), gestureName(next));
        m_stableGesture = next;
        if (m_callback) m_callback(m_stableGesture);
    }
    return m_stableGesture;
}

// ---------------------------------------------------------------------------
// Instant (single-frame) detection dispatcher
// ---------------------------------------------------------------------------
GestureType GestureRecognizer::detectInstant(const PoseResult& pose) {
    if (!pose.detected) return GestureType::None;

    if (detectVictory(pose))    return GestureType::Victory;
    if (detectHeartHands(pose)) return GestureType::HeartHands;
    if (detectThumbsUp(pose))   return GestureType::ThumbsUp;
    if (detectHandRaise(pose))  return GestureType::HandRaise;
    if (detectWave(pose))       return GestureType::Wave;
    return GestureType::None;
}

// ---------------------------------------------------------------------------
// Rule implementations
// ---------------------------------------------------------------------------

// ThumbsUp: wrist above elbow, elbow above shoulder (Y decreases upward in NDC)
bool GestureRecognizer::detectThumbsUp(const PoseResult& p) const {
    constexpr float kMinScore = 0.35f;
    auto check = [&](int wIdx, int eIdx, int sIdx) {
        const auto& w = p.keypoints[wIdx];
        const auto& e = p.keypoints[eIdx];
        const auto& s = p.keypoints[sIdx];
        if (!w.isValid(kMinScore) || !e.isValid(kMinScore) || !s.isValid(kMinScore))
            return false;
        // Y coordinates: smaller = higher in image (Y=0 top)
        return (w.y < e.y) && (e.y < s.y);
    };
    return check(POSE_LEFT_WRIST,  POSE_LEFT_ELBOW,  POSE_LEFT_SHOULDER)
        || check(POSE_RIGHT_WRIST, POSE_RIGHT_ELBOW, POSE_RIGHT_SHOULDER);
}

// HeartHands: both wrists close together AND both above shoulder level
bool GestureRecognizer::detectHeartHands(const PoseResult& p) const {
    constexpr float kMinScore   = 0.3f;
    constexpr float kMaxDistNDC = 0.10f; // wrists within 10% of frame width
    const auto& lw = p.leftWrist();
    const auto& rw = p.rightWrist();
    const auto& ls = p.leftShoulder();
    const auto& rs = p.rightShoulder();
    if (!lw.isValid(kMinScore) || !rw.isValid(kMinScore)) return false;
    if (!ls.isValid(kMinScore) || !rs.isValid(kMinScore)) return false;

    float wristDist = dist(lw, rw);
    float shoulderY = std::min(ls.y, rs.y);

    return (wristDist < kMaxDistNDC)
        && (lw.y < shoulderY)
        && (rw.y < shoulderY);
}

// Victory: both wrists above nose (hands fully raised)
bool GestureRecognizer::detectVictory(const PoseResult& p) const {
    constexpr float kMinScore = 0.3f;
    const auto& lw = p.leftWrist();
    const auto& rw = p.rightWrist();
    const auto& n  = p.nose();
    if (!lw.isValid(kMinScore) || !rw.isValid(kMinScore) || !n.isValid(kMinScore))
        return false;
    return (lw.y < n.y) && (rw.y < n.y);
}

// HandRaise: at least one wrist above its corresponding shoulder
bool GestureRecognizer::detectHandRaise(const PoseResult& p) const {
    constexpr float kMinScore = 0.3f;
    auto check = [&](int wIdx, int sIdx) {
        const auto& w = p.keypoints[wIdx];
        const auto& s = p.keypoints[sIdx];
        return w.isValid(kMinScore) && s.isValid(kMinScore) && (w.y < s.y);
    };
    return check(POSE_LEFT_WRIST, POSE_LEFT_SHOULDER)
        || check(POSE_RIGHT_WRIST, POSE_RIGHT_SHOULDER);
}

// Wave: dominant wrist above nose AND has significant horizontal movement
bool GestureRecognizer::detectWave(const PoseResult& p) {
    constexpr float kMinScore    = 0.3f;
    constexpr float kMinHorizMove= 0.08f; // 8% frame width over history window

    // Pick the higher wrist as the "active" hand
    const auto& lw = p.leftWrist();
    const auto& rw = p.rightWrist();
    const auto& nose = p.nose();

    float wristX = 0.f;
    bool  aboveNose = false;
    if (lw.isValid(kMinScore) && nose.isValid(kMinScore) && lw.y < nose.y) {
        wristX = lw.x; aboveNose = true;
    } else if (rw.isValid(kMinScore) && nose.isValid(kMinScore) && rw.y < nose.y) {
        wristX = rw.x; aboveNose = true;
    }
    if (!aboveNose) {
        // still update history so we don't lose context
        m_wristXHistory[m_wristHistoryIdx] = 0.f;
        m_wristHistoryIdx = (m_wristHistoryIdx + 1) % kWaveHistoryLen;
        return false;
    }

    m_wristXHistory[m_wristHistoryIdx] = wristX;
    m_wristHistoryIdx = (m_wristHistoryIdx + 1) % kWaveHistoryLen;
    if (m_wristHistoryIdx == 0) m_wristHistoryFull = true;

    if (!m_wristHistoryFull) return false;

    float xMin = *std::min_element(m_wristXHistory.begin(), m_wristXHistory.end());
    float xMax = *std::max_element(m_wristXHistory.begin(), m_wristXHistory.end());
    return (xMax - xMin) >= kMinHorizMove;
}

// ---------------------------------------------------------------------------
float GestureRecognizer::dist(const PoseKeypoint& a, const PoseKeypoint& b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace ai
} // namespace video
} // namespace sdk
