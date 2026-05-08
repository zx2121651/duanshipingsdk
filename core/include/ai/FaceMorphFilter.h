#pragma once
/**
 * FaceMorphFilter.h
 *
 * 实时人脸变形滤镜（瘦脸/大眼/下颌/额头/鼻翼/嘴唇等）。
 *
 * 算法：
 *   - 基于 FaceLandmarkDetector 106点 2D 坐标
 *   - 构建控制点驱动的 mesh 网格（全脸 UV grid，约 32×32）
 *   - 每个 mesh 顶点受附近 landmark 控制点影响（径向基函数 RBF 权重）
 *   - 在顶点着色器中位移 mesh，片元着色器正常采样输入纹理
 *   - CPU 每帧更新 mesh VBO（< 0.3ms），GPU draw call 极快
 *
 * 支持变形：
 *   SLIM_FACE      瘦脸（颧骨向内）
 *   BIG_EYES       大眼（眼周扩张）
 *   SLIM_JAW       下颌瘦（下巴收窄）
 *   FOREHEAD       额头收/扩
 *   NOSE_SLIM      鼻翼收窄
 *   MOUTH_SIZE     嘴巴大小
 *   EYE_DISTANCE   眼距调整
 *   CHIN_SHAPE     下巴拉长/缩短
 *
 * 用法：
 *   FaceMorphFilter filter;
 *   filter.setStrength(FaceMorphFilter::Effect::SLIM_FACE, 0.4f);
 *   filter.setStrength(FaceMorphFilter::Effect::BIG_EYES,  0.3f);
 *   filter.updateLandmarks(faceResult);
 *   // 每帧 processFrame(inputTex, outputFbo)
 */

#include "../Filter.h"
#include "FaceLandmarkDetector.h"
#include <array>
#include <vector>

namespace sdk {
namespace video {
namespace ai {

class FaceMorphFilter : public Filter {
public:
    enum class Effect {
        SLIM_FACE     = 0,
        BIG_EYES      = 1,
        SLIM_JAW      = 2,
        FOREHEAD      = 3,
        NOSE_SLIM     = 4,
        MOUTH_SIZE    = 5,
        EYE_DISTANCE  = 6,
        CHIN_SHAPE    = 7,
        COUNT         = 8,
    };

    FaceMorphFilter();
    ~FaceMorphFilter() override;

    // ── 参数 ──────────────────────────────────────────────────────────────

    /** 设置变形强度 [0, 1]，默认 0（无变形）。 */
    void setStrength(Effect effect, float strength);
    float getStrength(Effect effect) const;

    /** 重置所有强度为 0。 */
    void resetAll();

    // ── 人脸数据 ──────────────────────────────────────────────────────────

    /**
     * 每帧更新人脸 landmark（在 processFrame 之前调用）。
     * @param face  FaceLandmarkDetector 输出；detected=false 时 passthrough。
     */
    void updateLandmarks(const FaceResult& face);

    // ── Filter 接口 ──────────────────────────────────────────────────────

    std::string getVertexShaderName()   const override { return "face_morph.vert"; }
    std::string getFragmentShaderName() const override { return "face_morph.frag"; }

    Result initialize() override;
    void   onProgramRecompiled() override;
    ResultPayload<Texture> processFrame(const Texture& inputTexture,
                                         FrameBufferPtr outputFb) override;

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
    std::string getVertexShaderSource()   const override;

private:
    static constexpr int kGridW = 32;
    static constexpr int kGridH = 32;
    static constexpr int kVertexCount = (kGridW + 1) * (kGridH + 1);
    static constexpr int kIndexCount  = kGridW * kGridH * 6;

    // Strength for each effect
    std::array<float, static_cast<int>(Effect::COUNT)> m_strengths{};

    // Current face landmarks (normalized [0,1])
    FaceResult m_face;
    bool m_hasFace = false;

    // Mesh buffers
    GLuint m_meshVao = 0;
    GLuint m_meshVbo = 0;  // interleaved: pos(2f) + uv(2f)
    GLuint m_meshIbo = 0;
    bool   m_meshReady = false;

    // Uniform locations
    int m_locInputTex = -1;

    void buildBaseMesh();
    void updateMesh();
    void applyWarp(std::vector<float>& verts,
                   float cpX, float cpY,
                   float dx, float dy,
                   float radius) const;
    void cacheUniformLocations();
};

} // namespace ai
} // namespace video
} // namespace sdk
