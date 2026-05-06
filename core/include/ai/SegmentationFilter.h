#pragma once
/**
 * SegmentationFilter.h
 *
 * 人像分割滤镜 — 将 TfliteInferenceEngine 输出的 mask 纹理与原图合成：
 *
 *   模式 0 (BLUR_BG)   — 背景高斯模糊，前景保留原图
 *   模式 1 (REPLACE_BG) — 背景替换为纯色
 *
 * 接入现有 Filter 管线：
 *   processFrame(inputTexture, outputFb)
 *
 * 用法：
 *   auto seg = std::make_shared<SegmentationFilter>(inferenceEngine);
 *   seg->setParameter("mode", 0);                   // BLUR_BG
 *   seg->setParameter("blurStrength", 15.0f);        // 模糊半径
 *   seg->setParameter("bgColor", 0xFF000000u);       // REPLACE_BG 颜色
 *   seg->processFrame(inputTexture, outputFb);
 */

#include "../Filter.h"
#include "../FrameBufferPool.h"
#include "TfliteInferenceEngine.h"
#include <memory>

namespace sdk {
namespace video {

class SegmentationFilter : public Filter {
public:
    explicit SegmentationFilter(
        std::shared_ptr<ai::TfliteInferenceEngine> engine,
        FrameBufferPool* pool = nullptr);
    ~SegmentationFilter() override;

    std::string getVertexShaderName()   const override { return "segmentation.vert"; }
    std::string getFragmentShaderName() const override { return "segmentation.frag"; }

    Result initialize() override;
    void   onProgramRecompiled() override;

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

    // Uniform locations
    GLuint m_locInputTex   = 0;
    GLuint m_locMaskTex    = 0;
    GLuint m_locMode       = 0;
    GLuint m_locBgColor    = 0;
    GLuint m_locEdgeSoften = 0;

    // Cached inference result
    GLuint m_lastMaskTexId = 0;

    void cacheUniformLocations();
};

} // namespace video
} // namespace sdk
