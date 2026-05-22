#pragma once
/**
 * ExpressionDetector.h
 *
 * 基于 FaceLandmarkDetector 106 点输出的轻量规则表情识别器。
 *
 * 无需额外 AI 模型——通过几何规则在 CPU 上实时运行（<0.1ms/frame）。
 *
 * 支持表情：
 *   SMILE       微笑（嘴角上扬）
 *   MOUTH_OPEN  张嘴（上下唇距离 > 阈值）
 *   BLINK_LEFT  左眼眨眼（左眼高度 < 阈值）
 *   BLINK_RIGHT 右眼眨眼
 *   BROW_RAISE  挑眉（眉毛与眼睛距离增大）
 *   WINK_LEFT   左眼眨眼但右眼睁开（≈ 左眼闭合 + 右眼开）
 *   WINK_RIGHT  右眼闭合 + 左眼开
 *
 * 用法：
 *   ExpressionDetector expr;
 *   auto faceResult = faceLandmarkDetector.getLatestResult();
 *   if (!faceResult.faces.empty()) {
 *       auto exprSet = expr.detect(faceResult.faces[0]);
 *       if (exprSet.smiling)    { ... }
 *       if (exprSet.blinkLeft)  { ... }
 *   }
 */

#include "FaceLandmarkDetector.h"
#include <cstdint>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// ExpressionResult — 单帧检测结果（多表情可同时激活）
// ---------------------------------------------------------------------------
struct ExpressionResult {
    bool smiling     = false;
    bool mouthOpen   = false;
    bool blinkLeft   = false;
    bool blinkRight  = false;
    bool browRaise   = false;
    bool winkLeft    = false;
    bool winkRight   = false;

    // 连续量（[0,1]），可驱动 AR 形变
    float smileIntensity    = 0.f;
    float mouthOpenness     = 0.f;  ///< 0=闭合, 1=最大张嘴
    float leftEyeOpenness   = 1.f;  ///< 0=完全闭合, 1=完全睁开
    float rightEyeOpenness  = 1.f;
    float browRaiseAmount   = 0.f;
};

// ---------------------------------------------------------------------------
// ExpressionDetector
// ---------------------------------------------------------------------------
class ExpressionDetector {
public:
    ExpressionDetector() = default;
    ~ExpressionDetector() = default;

    /**
     * 从单个 FaceResult 计算表情。
     * @param face  FaceLandmarkDetector 输出的人脸结果
     * @return      表情检测结果
     */
    ExpressionResult detect(const FaceResult& face) const;

    // ── 阈值调节 ─────────────────────────────────────────────────────────

    /** 微笑检测阈值（嘴角 Y 偏移 / 人脸高度），默认 0.02。 */
    void setSmileThreshold(float t)     { m_smileThresh   = t; }
    /** 张嘴检测阈值（上下唇距 / 人脸高度），默认 0.04。 */
    void setMouthOpenThresh(float t)    { m_mouthOpenThresh = t; }
    /** 眨眼检测阈值（眼睛高度 / 人脸高度），默认 0.018。 */
    void setBlinkThreshold(float t)     { m_blinkThresh   = t; }
    /** 挑眉检测阈值（眉眼距变化 / 人脸高度），默认 0.015。 */
    void setBrowRaiseThreshold(float t) { m_browRaiseThresh = t; }

private:
    float m_smileThresh    = 0.020f;
    float m_mouthOpenThresh= 0.040f;
    float m_blinkThresh    = 0.018f;
    float m_browRaiseThresh= 0.015f;

    // 计算两点欧氏距离（归一化空间）
    static float dist(const FaceLandmark& a, const FaceLandmark& b);
    // 人脸归一化基准长度（双眼距离）
    static float faceScale(const FaceResult& f);
};

} // namespace ai
} // namespace video
} // namespace sdk
