#pragma once
/**
 * FaceReshapeFilter.h
 *
 * GPU Mesh Warp 人脸重塑滤镜 — 对标抖音"大眼/瘦脸/额头/下颌/瘦鼻/嘴型"效果。
 *
 * 原理：
 *   1. 由 FaceLandmarkDetector 提供 106 点关键坐标
 *   2. 在 GPU 上构建 128×128 位移网格（displacement mesh）
 *   3. Vertex Shader 根据控制点 + 影响半径计算每个网格顶点的位移量
 *   4. Fragment Shader 采样扭曲后的输入纹理
 *
 * 参数（均 [0, 1] 范围，0 = 无效果）：
 *   eyeScale   — 大眼（默认 0.0）
 *   faceSlim   — 瘦脸（默认 0.0）
 *   noseSlim   — 瘦鼻（默认 0.0）
 *   foreheadUp — 额头增高（默认 0.0）
 *   chinV      — 下颌收窄/V 脸（默认 0.0）
 *   mouthWidth — 嘴型宽度调整（默认 0.0，正=宽/负=窄）
 */

#include "../Filter.h"
#include "FaceLandmarkDetector.h"

namespace sdk {
namespace video {

class FaceReshapeFilter : public Filter {
public:
    std::string getVertexShaderName()   const override { return "face_reshape.vert"; }
    std::string getFragmentShaderName() const override { return "face_reshape.frag"; }

    FaceReshapeFilter();
    ~FaceReshapeFilter() override;

    Result initialize() override;
    void   onProgramRecompiled() override;

    // ---- 关键点输入 ----
    /** 每帧渲染前调用，传入最新关键点结果 */
    void setLandmarkResult(const ai::LandmarkFrameResult& result);

    // ---- 效果强度参数（通过 setParameter 或 set* 直接设置）----
    void setEyeScale   (float v) { m_eyeScale   = v; }
    void setFaceSlim   (float v) { m_faceSlim   = v; }
    void setNoseSlim   (float v) { m_noseSlim   = v; }
    void setForeheadUp (float v) { m_foreheadUp = v; }
    void setChinV      (float v) { m_chinV      = v; }
    void setMouthWidth (float v) { m_mouthWidth = v; }

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getVertexShaderSource()   const override;
    std::string getFragmentShaderSource() const override;

private:
    // 效果强度
    float m_eyeScale   = 0.f;
    float m_faceSlim   = 0.f;
    float m_noseSlim   = 0.f;
    float m_foreheadUp = 0.f;
    float m_chinV      = 0.f;
    float m_mouthWidth = 0.f;

    // 最新人脸关键点（单人）
    ai::FaceResult m_faceResult;
    bool m_hasFace = false;

    // 位移网格 VBO
    static constexpr int kGridW = 128;
    static constexpr int kGridH = 128;
    GLuint m_vbo      = 0;
    GLuint m_ebo      = 0;
    GLuint m_vao      = 0;

    // Uniform 位置
    GLint  m_uTexture       = -1;
    GLint  m_uLandmarks     = -1; // vec2[106]
    GLint  m_uEyeScale      = -1;
    GLint  m_uFaceSlim      = -1;
    GLint  m_uNoseSlim      = -1;
    GLint  m_uForeheadUp    = -1;
    GLint  m_uChinV         = -1;
    GLint  m_uMouthWidth    = -1;
    GLint  m_uHasFace       = -1;

    void buildMesh();
    void updateMesh();
    void cacheUniforms();
};

} // namespace video
} // namespace sdk
