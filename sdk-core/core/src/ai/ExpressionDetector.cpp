/**
 * ExpressionDetector.cpp
 *
 * 基于 106 点人脸关键点的规则表情检测。
 *
 * 关键点索引（MediaPipe-compatible 106点子集）：
 *   眼睛：
 *     左眼上眼睑 ≈ 37, 38；左眼下眼睑 ≈ 41, 40
 *     右眼上眼睑 ≈ 43, 44；右眼下眼睑 ≈ 47, 46
 *     左眼外角 = 36, 内角 = 39；右眼内角 = 42, 外角 = 45
 *   眉毛：
 *     左眉中点 ≈ 19；右眉中点 ≈ 24
 *   嘴巴：
 *     嘴角左 = 48, 右 = 54
 *     上唇中 = 51；下唇中 = 57
 *   参考基准：
 *     鼻尖 = 30；下颌 = 8
 */

#include "../../include/ai/ExpressionDetector.h"
#include <cmath>
#include <algorithm>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
float ExpressionDetector::dist(const FaceLandmark& a, const FaceLandmark& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float ExpressionDetector::faceScale(const FaceResult& f) {
    if (f.landmarks.size() < 46) return 1.f;
    // Inter-ocular distance: left eye outer (36) to right eye outer (45)
    float d = dist(f.landmarks[36], f.landmarks[45]);
    return std::max(d, 1e-4f);
}

// ---------------------------------------------------------------------------
// detect()
// ---------------------------------------------------------------------------
ExpressionResult ExpressionDetector::detect(const FaceResult& face) const {
    ExpressionResult out;
    if (!face.detected || face.landmarks.size() < 68) return out;

    const auto& lm  = face.landmarks;
    float scale     = faceScale(face);

    // ── 左眼开合度（上下眼睑距离 / 眼宽）────────────────────────────────
    // 左眼竖向: lm[37]~lm[41], lm[38]~lm[40]
    float leftEyeV  = (dist(lm[37], lm[41]) + dist(lm[38], lm[40])) * 0.5f;
    // 左眼横向 (outer to inner corner): lm[36] ~ lm[39]
    float leftEyeH  = dist(lm[36], lm[39]);
    float leftEAR   = (leftEyeH > 1e-5f) ? (leftEyeV / leftEyeH) : 0.f;
    // Typical open EAR ≈ 0.3; closed ≈ 0.1
    out.leftEyeOpenness = std::min(leftEAR / 0.35f, 1.f);
    out.blinkLeft  = (leftEyeV / scale) < m_blinkThresh;

    // ── 右眼 ──────────────────────────────────────────────────────────────
    float rightEyeV = (dist(lm[43], lm[47]) + dist(lm[44], lm[46])) * 0.5f;
    float rightEyeH = dist(lm[42], lm[45]);
    float rightEAR  = (rightEyeH > 1e-5f) ? (rightEyeV / rightEyeH) : 0.f;
    out.rightEyeOpenness = std::min(rightEAR / 0.35f, 1.f);
    out.blinkRight = (rightEyeV / scale) < m_blinkThresh;

    // ── 眨眼/眨右眼 ───────────────────────────────────────────────────────
    out.winkLeft  = out.blinkLeft  && !out.blinkRight;
    out.winkRight = out.blinkRight && !out.blinkLeft;

    // ── 张嘴（上下唇中点距离 / 人脸基准）────────────────────────────────
    float mouthV = dist(lm[51], lm[57]);
    out.mouthOpenness = std::min(mouthV / (scale * 0.35f), 1.f);
    out.mouthOpen = (mouthV / scale) > m_mouthOpenThresh;

    // ── 微笑（嘴角 Y 比上唇中心高）──────────────────────────────────────
    // lm[48]=左嘴角, lm[54]=右嘴角, lm[51]=上唇中点
    float mouthCenterY = (lm[48].y + lm[54].y) * 0.5f;
    float upperLipY    = lm[51].y;
    float smileDelta   = (upperLipY - mouthCenterY) / scale; // 微笑时为正
    out.smileIntensity = std::max(0.f, smileDelta / (m_smileThresh * 2.f));
    out.smileIntensity = std::min(out.smileIntensity, 1.f);
    out.smiling        = smileDelta > m_smileThresh;

    // ── 挑眉（眉毛中点 Y 与眼睛上眼睑 Y 差值增大）───────────────────────
    // lm[19] = 左眉中点; lm[37] = 左眼上眼睑
    // lm[24] = 右眉中点; lm[43] = 右眼上眼睑
    float leftBrowEyeDist  = std::fabs(lm[37].y - lm[19].y) / scale;
    float rightBrowEyeDist = std::fabs(lm[43].y - lm[24].y) / scale;
    float avgBrowDist = (leftBrowEyeDist + rightBrowEyeDist) * 0.5f;
    // Baseline approximately 0.10; raise threshold is above that
    out.browRaiseAmount = std::max(0.f, (avgBrowDist - 0.08f) / 0.05f);
    out.browRaiseAmount = std::min(out.browRaiseAmount, 1.f);
    out.browRaise       = avgBrowDist > (0.08f + m_browRaiseThresh);

    return out;
}

} // namespace ai
} // namespace video
} // namespace sdk
