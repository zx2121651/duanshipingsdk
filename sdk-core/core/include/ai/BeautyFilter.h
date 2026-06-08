#pragma once
/**
 * BeautyFilter.h
 *
 * 磨皮 + 美白滤镜 — 单 pass GPU 实现。
 *
 * 算法：
 *   1. 皮肤检测：基于 YCbCr 色彩范围（Cb∈[77,127], Cr∈[133,173]）生成皮肤蒙版
 *   2. 磨皮：对皮肤区域做近似双边滤波（局部方差引导，保护边缘）
 *   3. 美白：亮度曲线提升（仅作用于皮肤区域）
 *
 * 单 pass 完成：避免多次 FBO 切换，在中端机上 < 3ms/帧（1080p）
 *
 * 参数：
 *   smoothStrength — 磨皮强度 [0.0, 1.0]，默认 0.6
 *   whitenStrength — 美白强度 [0.0, 1.0]，默认 0.4
 *
 * 用法：
 *   auto beauty = std::make_shared<BeautyFilter>();
 *   beauty->setParameter("smoothStrength", 0.7f);
 *   beauty->setParameter("whitenStrength", 0.3f);
 *   beauty->processFrame(inputTexture, outputFb);
 */

#include "../Filter.h"
#include "../pipeline/PipelineNode.h"
#include "InferenceEngine.h"
#include "FaceLandmarkDetector.h"
#include <memory>


namespace sdk {
namespace video {

class BeautyFilter : public Filter, public PipelineNode {
public:
    BeautyFilter();
    ~BeautyFilter() override = default;

    std::string getVertexShaderName()   const override { return "beauty.vert"; }
    std::string getFragmentShaderName() const override { return "beauty.frag"; }

    Result initialize() override;
    void   onProgramRecompiled() override;


    // PipelineNode override
    ResultPayload<VideoFrame> pullFrame(int64_t timestampNs) override;

    // Set inference engine
    void setInferenceEngine(std::shared_ptr<ai::InferenceEngine> engine) { m_inferenceEngine = engine; }

    void setLandmarkResult(const ai::LandmarkFrameResult& r);

    void release() override;
protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
    std::string getVertexShaderSource()   const override;

private:
    struct UniformParams {
        float smoothStrength = 0.6f;
        float whitenStrength = 0.4f;
        float texelWidth = 0.001f;
        float texelHeight = 0.001f;
        float hasFace = 0.0f;
        float faceCenterX = 0.5f;
        float faceCenterY = 0.5f;
        float reserved0 = 0.0f;
        float faceRadiusX = 0.5f;
        float faceRadiusY = 0.5f;
        float reserved1 = 0.0f;
        float reserved2 = 0.0f;
    };

    GLuint m_locSmoothStrength = 0;
    GLuint m_locWhitenStrength = 0;
    GLuint m_locTexelSize      = 0;
    GLint  m_locFaceCenter     = -1;
    GLint  m_locFaceRadius     = -1;
    GLint  m_locHasFace        = -1;

    bool  m_hasFace = false;
    float m_faceCenterX = 0.5f;
    float m_faceCenterY = 0.5f;
    float m_faceRadiusX = 0.5f;
    float m_faceRadiusY = 0.5f;

    void cacheUniformLocations();
    UniformParams buildUniformParams(const Texture& inputTexture) const;
    void uploadLegacyUniforms(const UniformParams& params) const;

    std::shared_ptr<ai::InferenceEngine> m_inferenceEngine;
    std::shared_ptr<rhi::IBuffer> m_paramsBuffer;

    // Dual PBOs for async pixel readback
    GLuint m_pbos[2] = {0, 0};
    int m_pboIndex = 0;

    // Dummy VBO for mesh vertices to demonstrate dynamic upload
    GLuint m_meshVbo = 0;

    void enqueueAsyncReadPixelsToInference(const VideoFrame& inputFrame);

};

} // namespace video
} // namespace sdk
