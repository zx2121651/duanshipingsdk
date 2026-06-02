#pragma once
/**
 * SegmentationFilter.h
 *
 * 人像分割滤镜 — 将 TfliteInferenceEngine 输出的 mask 纹理与原图合成：
 *
 *   模式 0 (BLUR_BG)      — 背景高斯模糊，前景保留原图
 *   模式 1 (REPLACE_BG)   — 背景替换为纯色（bgColor 参数）
 *   模式 2 (TRANSPARENT)  — 背景透明（RGBA alpha=0），适合导出带透明通道的叠加层
 *   模式 3 (IMAGE_BG)     — 背景替换为指定纹理（bgImageTexId 参数）
 *
 * 接入现有 Filter 管线：
 *   processFrame(inputTexture, outputFb)
 *
 * 用法：
 *   auto seg = std::make_shared<SegmentationFilter>(inferenceEngine);
 *   seg->setParameter("mode", 0);                   // BLUR_BG
 *   seg->setParameter("blurStrength", 15.0f);        // 模糊半径
 *   seg->setParameter("bgColor", 0xFF000000u);       // REPLACE_BG 颜色（ARGB）
 *   seg->setBgImageTexture(bgTexId);                 // IMAGE_BG 背景纹理
 *   seg->processFrame(inputTexture, outputFb);
 */

#include "../Filter.h"
#include "../Filters.h"
#include "../FrameBufferPool.h"
#include "TfliteInferenceEngine.h"
#include <memory>

namespace sdk {
namespace video {

class SegmentationFilter : public Filter {
public:
    /** 背景处理模式（与 shader u_mode 保持一致）。 */
    enum class Mode : int {
        BLUR        = 0,  ///< 高斯模糊背景
        BG_COLOR    = 1,  ///< 纯色背景
        TRANSPARENT = 2,  ///< 背景透明（alpha=0）
        BG_IMAGE    = 3,  ///< 图像纹理背景
        ORIGINAL    = 4,  ///< 原图直通
    };

    explicit SegmentationFilter(
        std::shared_ptr<ai::TfliteInferenceEngine> engine,
        FrameBufferPool* pool = nullptr);
    ~SegmentationFilter() override;

    /** 设置 BG_IMAGE 模式下的背景纹理 ID（已上传到 GPU 的 GL 纹理）。 */
    void setBgImageTexture(GLuint texId);
    GLuint getBgImageTexture() const { return m_bgImageTexId; }

    Mode getMode() const;
    float getBlurStrength() const;
    uint32_t getBgColor() const;
    float getEdgeSoften() const;

    std::string getVertexShaderName()   const override { return "segmentation.vert"; }
    std::string getFragmentShaderName() const override { return "segmentation.frag"; }

    Result initialize() override;
    void   onProgramRecompiled() override;

    void setParameter(const std::string& key, const std::any& value) override;

    // 重写：推理 → 合成，双 pass（原图 + mask → 输出）
    ResultPayload<Texture> processFrame(const Texture& inputTexture,
                                        FrameBufferPtr outputFb) override;

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
    std::string getVertexShaderSource()   const override;

private:
    std::shared_ptr<ai::TfliteInferenceEngine> m_engine;
    FrameBufferPool* m_pool;

    std::unique_ptr<GaussianBlurFilter> m_blurFilter;
    FrameBufferPtr m_blurredFb;

    // Uniform locations
    GLuint m_locInputTex    = 0;
    GLuint m_locMaskTex     = 0;
    GLuint m_locMode        = 0;
    GLuint m_locBgColor     = 0;
    GLuint m_locEdgeSoften  = 0;
    GLuint m_locBgImageTex  = 0;  ///< IMAGE_BG 模式背景纹理 uniform

    // Cached inference result
    GLuint m_lastMaskTexId  = 0;

    // IMAGE_BG 模式：外部传入的背景纹理 ID
    GLuint m_bgImageTexId   = 0;

    void cacheUniformLocations();
};

} // namespace video
} // namespace sdk
