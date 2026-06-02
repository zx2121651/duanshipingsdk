#pragma once
/**
 * MakeupFilter.h
 *
 * GPU AI 美妆滤镜 — 对标抖音口红/腮红/眼影/高光/修容/眉毛效果。
 *
 * 渲染流程：
 *   输入帧 → 皮肤蒙版生成（基于关键点）→ 多层叠加混合 → 输出
 *
 * 各妆容层混合模式：
 *   口红    — Overlay（透明度 alpha × lipIntensity）
 *   腮红    — Screen（自然透明叠加）
 *   眼影    — Multiply（暗色叠加）
 *   高光    — Add（提亮）
 *   修容/阴影 — Multiply（压暗轮廓）
 *   眉毛    — Normal（直接覆盖）
 *
 * 参数全部通过 setParameter(key, std::any(float)) 传入。
 */

#include "../Filter.h"
#include "FaceLandmarkDetector.h"

namespace sdk {
namespace video {

// 颜色值 + 强度
struct MakeupLayer {
    float r = 0.f, g = 0.f, b = 0.f;
    float intensity = 0.f;  ///< [0,1]
    bool  enabled() const { return intensity > 0.001f; }
};

class MakeupFilter : public Filter {
public:
    std::string getVertexShaderName()   const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "makeup.frag"; }

    MakeupFilter();
    ~MakeupFilter() override = default;

    Result initialize() override;
    void   onProgramRecompiled() override;

    /** 每帧渲染前传入最新关键点 */
    void setLandmarkResult(const ai::LandmarkFrameResult& result);

    // ---- 妆容设置 ----
    void setLipColor  (float r, float g, float b, float intensity);
    void setBlush     (float r, float g, float b, float intensity);
    void setEyeshadow (float r, float g, float b, float intensity);
    void setHighlight (float intensity);               ///< 高光（白色）
    void setContour   (float intensity);               ///< 修容（暗棕）
    void setEyebrow   (float r, float g, float b, float intensity);

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;

private:
    MakeupLayer m_lip;
    MakeupLayer m_blush;
    MakeupLayer m_eyeshadow;
    MakeupLayer m_highlight;
    MakeupLayer m_contour;
    MakeupLayer m_eyebrow;

    ai::FaceResult m_faceResult;
    bool m_hasFace = false;

    // Uniform 位置
    GLint m_uTexture       = -1;
    GLint m_uHasFace       = -1;
    GLint m_uLandmarks     = -1;
    GLint m_uLipColor      = -1;
    GLint m_uLipIntensity  = -1;
    GLint m_uBlushColor    = -1;
    GLint m_uBlushIntensity= -1;
    GLint m_uEyeColor      = -1;
    GLint m_uEyeIntensity  = -1;
    GLint m_uHighlight     = -1;
    GLint m_uContour       = -1;
    GLint m_uEyebrowColor  = -1;
    GLint m_uEyebrowIntens = -1;

    void cacheUniforms();
};

} // namespace video
} // namespace sdk
