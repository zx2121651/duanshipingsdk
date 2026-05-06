#pragma once
/**
 * HairSegmentationFilter.h
 *
 * 发色染色滤镜 — 对标抖音"染发"特效。
 *
 * 原理：
 *   1. TFLite 发色分割模型输出头发蒙版（GL_R8 纹理）
 *   2. Fragment Shader 对蒙版区域叠加目标发色（HLS 色相替换）
 *
 * 参数：
 *   hairColor    [R,G,B]  — 目标发色
 *   colorIntensity [0,1]  — 染色强度
 *   glossIntensity [0,1]  — 高光强度（使头发更有光泽感）
 */

#include "../Filter.h"
#include "TfliteInferenceEngine.h"

namespace sdk {
namespace video {

class HairSegmentationFilter : public Filter {
public:
    std::string getVertexShaderName()   const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "hair_color.frag"; }

    HairSegmentationFilter();
    ~HairSegmentationFilter() override;

    Result initialize() override;
    void   onProgramRecompiled() override;

    /**
     * 加载发色分割 TFLite 模型。
     * @param modelPath  .tflite 文件路径
     */
    bool loadModel(const std::string& modelPath);
    bool loadModelFromBuffer(const void* data, size_t size);

    /** 设置目标发色 RGB [0,1] */
    void setHairColor(float r, float g, float b);
    void setColorIntensity(float v) { m_colorIntensity = v; }
    void setGlossIntensity(float v) { m_glossIntensity = v; }

    ResultPayload<Texture> processFrame(
        const Texture& inputTexture, FrameBufferPtr outputFb) override;

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;

private:
    ai::TfliteInferenceEngine m_engine;

    float m_hairR = 0.20f, m_hairG = 0.05f, m_hairB = 0.50f; // 默认紫色
    float m_colorIntensity = 0.7f;
    float m_glossIntensity = 0.3f;

    GLuint m_maskTexId = 0;
    int    m_maskW     = 0;
    int    m_maskH     = 0;

    GLint  m_uInput         = -1;
    GLint  m_uMask          = -1;
    GLint  m_uHairColor     = -1;
    GLint  m_uColorIntensity= -1;
    GLint  m_uGloss         = -1;

    void cacheUniforms();
};

} // namespace video
} // namespace sdk
