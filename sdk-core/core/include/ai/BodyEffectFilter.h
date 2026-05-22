#pragma once
/**
 * BodyEffectFilter.h
 *
 * 身体特效滤镜（长腿/瘦身/放大头/缩肩）。
 *
 * 基于 BodyPoseDetector 17点 COCO 骨骼点驱动 mesh 变形，
 * 与 FaceMorphFilter 使用相同的 mesh warp 算法。
 *
 * 支持效果：
 *   SLIM_BODY     瘦身（躯干横向收窄）
 *   LONG_LEGS     长腿（膝盖以下纵向拉伸）
 *   SMALL_HEAD    小头（头部缩小）
 *   SLIM_SHOULDER 窄肩（肩宽收窄）
 *   LIFT_BUTTOCKS 提臀（臀部上移）
 */

#include "../Filter.h"
#include <array>
#include <vector>
#include <cstdint>

namespace sdk {
namespace video {
namespace ai {

// Re-use BodyPose point type (17-point COCO format)
struct BodyPosePoint {
    float x = 0.f, y = 0.f, confidence = 0.f;
};

struct BodyPoseResult {
    bool detected = false;
    std::array<BodyPosePoint, 17> keypoints{};
    // COCO indices: 0=nose,1=lEye,2=rEye,3=lEar,4=rEar,
    //               5=lShoulder,6=rShoulder,7=lElbow,8=rElbow,
    //               9=lWrist,10=rWrist,11=lHip,12=rHip,
    //               13=lKnee,14=rKnee,15=lAnkle,16=rAnkle
};

class BodyEffectFilter : public Filter {
public:
    enum class Effect {
        SLIM_BODY      = 0,
        LONG_LEGS      = 1,
        SMALL_HEAD     = 2,
        SLIM_SHOULDER  = 3,
        LIFT_BUTTOCKS  = 4,
        COUNT          = 5,
    };

    BodyEffectFilter();
    ~BodyEffectFilter() override;

    void setStrength(Effect e, float v);
    float getStrength(Effect e) const;
    void resetAll();

    /** 每帧更新身体骨骼点（在 processFrame 之前调用）。 */
    void updatePose(const BodyPoseResult& pose);

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
    static constexpr int kVertexCount = (kGridW+1)*(kGridH+1);
    static constexpr int kIndexCount  = kGridW*kGridH*6;

    std::array<float, static_cast<int>(Effect::COUNT)> m_strengths{};
    BodyPoseResult m_pose;
    bool m_hasPose = false;

    GLuint m_meshVao=0, m_meshVbo=0, m_meshIbo=0;
    bool   m_meshReady = false;
    int    m_locInputTex = -1;

    void buildBaseMesh();
    void updateMesh();
    void applyWarp(std::vector<float>& verts,
                   float cpX, float cpY, float dx, float dy, float radius) const;
    void applyStretch(std::vector<float>& verts,
                      float belowY, float scale) const;
    void cacheUniformLocations();
};

} // namespace ai
} // namespace video
} // namespace sdk
