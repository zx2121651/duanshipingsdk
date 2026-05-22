#pragma once
/**
 * LandmarkSmoother.h
 *
 * 人脸/身体关键点时域平滑器 — 解决 TFLite 帧间抖动问题。
 *
 * 算法：自适应指数移动平均（Adaptive EMA）
 *   smoothed_t = alpha(v) * observed_t + (1 - alpha(v)) * smoothed_{t-1}
 *
 *   其中 alpha(v) 随关键点运动速度动态调整：
 *     - 运动慢 → alpha 小 → 平滑更强（抑制静止抖动）
 *     - 运动快 → alpha 大 → 响应更快（避免拖影）
 *
 * 典型参数：
 *   minAlpha = 0.05  — 完全静止时的最小平滑系数（可调低到 0.03）
 *   maxAlpha = 0.6   — 快速移动时的最大平滑系数
 *   velThreshLow  = 0.002 — 低于此归一化速度视为静止
 *   velThreshHigh = 0.04  — 高于此归一化速度视为快速移动
 *
 * 用法：
 *   LandmarkSmoother smoother;
 *   smoother.reset();
 *   // 每帧调用：
 *   LandmarkFrameResult stable = smoother.smooth(rawResult);
 */

#include "FaceLandmarkDetector.h"
#include <array>
#include <cmath>

namespace sdk {
namespace video {
namespace ai {

struct SmootherConfig {
    float minAlpha     = 0.05f; ///< 静止时最小平滑系数（越小越平滑，但响应越慢）
    float maxAlpha     = 0.60f; ///< 快速运动时最大系数（越大响应越快，但抖动增大）
    float velThreshLow = 0.003f;///< 低速门限（NDC 每帧）
    float velThreshHigh= 0.05f; ///< 高速门限（NDC 每帧）
};

// ---------------------------------------------------------------------------
// 单面孔平滑器
// ---------------------------------------------------------------------------
class FaceSmoother {
public:
    explicit FaceSmoother(SmootherConfig cfg = {}) : m_cfg(cfg) { reset(); }

    /** 重置历史状态（切换人脸或场景切换时调用）。 */
    void reset() {
        m_initialized = false;
        m_prev = FaceResult{};
    }

    /**
     * 对一帧 FaceResult 进行时域平滑。
     * @param raw   本帧检测原始结果
     * @return      平滑后的结果（坐标 + score 都已平滑）
     */
    FaceResult smooth(const FaceResult& raw) {
        if (!raw.detected) {
            // 检测失败：按指数衰减平滑分数，坐标保持上一帧
            if (m_initialized) {
                FaceResult out = m_prev;
                out.faceScore *= 0.8f;
                out.detected = (out.faceScore > 0.1f);
                m_prev = out;
                return out;
            }
            return raw;
        }

        if (!m_initialized) {
            m_prev = raw;
            m_initialized = true;
            return raw;
        }

        FaceResult out = raw;
        for (int i = 0; i < kFaceLandmarkCount; ++i) {
            const auto& cur  = raw.landmarks[i];
            const auto& prev = m_prev.landmarks[i];

            float dx = cur.x - prev.x;
            float dy = cur.y - prev.y;
            float vel = std::sqrt(dx * dx + dy * dy);

            float alpha = adaptiveAlpha(vel);
            out.landmarks[i].x     = alpha * cur.x     + (1.f - alpha) * prev.x;
            out.landmarks[i].y     = alpha * cur.y     + (1.f - alpha) * prev.y;
            out.landmarks[i].score = alpha * cur.score + (1.f - alpha) * prev.score;
        }

        // Smooth bounding box
        for (int i = 0; i < 4; ++i)
            out.boundingBox[i] = m_cfg.maxAlpha * raw.boundingBox[i]
                               + (1.f - m_cfg.maxAlpha) * m_prev.boundingBox[i];

        out.faceScore = 0.5f * raw.faceScore + 0.5f * m_prev.faceScore;
        m_prev = out;
        return out;
    }

    void setConfig(const SmootherConfig& cfg) { m_cfg = cfg; }

private:
    SmootherConfig m_cfg;
    FaceResult     m_prev;
    bool           m_initialized = false;

    // Map velocity → alpha using linear interpolation between thresholds
    float adaptiveAlpha(float vel) const {
        if (vel <= m_cfg.velThreshLow)  return m_cfg.minAlpha;
        if (vel >= m_cfg.velThreshHigh) return m_cfg.maxAlpha;
        float t = (vel - m_cfg.velThreshLow)
                / (m_cfg.velThreshHigh - m_cfg.velThreshLow);
        return m_cfg.minAlpha + t * (m_cfg.maxAlpha - m_cfg.minAlpha);
    }
};

// ---------------------------------------------------------------------------
// 多人帧平滑器（包装 LandmarkFrameResult，最多 kMaxFaces 人）
// ---------------------------------------------------------------------------
class LandmarkSmoother {
public:
    explicit LandmarkSmoother(SmootherConfig cfg = {}) {
        for (auto& s : m_faceSmoothers) s = FaceSmoother(cfg);
    }

    /** 重置所有面孔的历史状态。 */
    void reset() {
        for (auto& s : m_faceSmoothers) s.reset();
    }

    /**
     * 平滑整帧结果。
     * 人脸数量变化时，旧槽位自动重置。
     */
    LandmarkFrameResult smooth(const LandmarkFrameResult& raw) {
        LandmarkFrameResult out = raw;
        // reset smoothers for slots beyond current face count
        for (int i = raw.faceCount; i < kMaxFaces; ++i)
            m_faceSmoothers[i].reset();
        // smooth each detected face
        for (int i = 0; i < raw.faceCount && i < kMaxFaces; ++i)
            out.faces[i] = m_faceSmoothers[i].smooth(raw.faces[i]);
        return out;
    }

    void setConfig(const SmootherConfig& cfg) {
        for (auto& s : m_faceSmoothers) s.setConfig(cfg);
    }

private:
    std::array<FaceSmoother, kMaxFaces> m_faceSmoothers;
};

} // namespace ai
} // namespace video
} // namespace sdk
