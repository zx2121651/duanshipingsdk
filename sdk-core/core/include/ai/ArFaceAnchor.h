#pragma once
/**
 * ArFaceAnchor.h
 *
 * 3D AR 人脸锚点计算器（无需 Sceneform / filament）。
 *
 * 基于 FaceLandmarkDetector 106 点 2D 坐标，通过透视 n 点（PnP）估计
 * 人脸在相机坐标系中的 6DOF 位姿（旋转 + 平移），供 3D 特效渲染使用。
 *
 * 架构：
 *   - 纯 C++ CPU 实现，不依赖 OpenCV（内置简化 DLT + Rodrigues）
 *   - 可选注入高精度 PnP solver（如 OpenCV solvePnP）替换内置实现
 *
 * 输出 FaceAnchor：
 *   - MVP 矩阵（列主序 float[16]，直接送入 OpenGL glUniformMatrix4fv）
 *   - 欧拉角（pitch/yaw/roll，弧度）
 *   - 相机坐标系 3D 位置（x, y, depth）
 *   - 是否有效（解算收敛）
 *
 * 用法：
 *   ArFaceAnchor anchor;
 *   anchor.setImageSize(1080, 1920);
 *   anchor.setFocalLength(1280.f);  // 估算值；Camera2 可查实际焦距
 *   auto result = anchor.estimate(faceResult);
 *   if (result.valid) {
 *       glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, result.mvpMatrix);
 *   }
 */

#include "FaceLandmarkDetector.h"
#include <array>
#include <functional>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// 3D 人脸模型关键点（参考坐标系，单位：厘米）
// 基于 Generic 3D Face Model (Basel Face Model subset)
// ---------------------------------------------------------------------------
struct FaceModelPoint3D {
    float x, y, z;
};

// ---------------------------------------------------------------------------
// FaceAnchor — 单帧解算结果
// ---------------------------------------------------------------------------
struct FaceAnchor {
    bool  valid        = false;

    // 6-DOF 位姿（相机坐标系）
    float tx = 0.f, ty = 0.f, tz = 0.f;  ///< 平移（厘米）
    float pitch = 0.f;                    ///< 俯仰角（弧度），抬头为正
    float yaw   = 0.f;                    ///< 偏航角（弧度），右转为正
    float roll  = 0.f;                    ///< 翻滚角（弧度）

    // MVP 矩阵（列主序 float[16]，可直接传入 OpenGL）
    float modelMatrix[16]{};
    float viewMatrix[16]{};
    float mvpMatrix[16]{};

    // 旋转矩阵（行主序 3×3，供非 GL 渲染器使用）
    float rotationMatrix[9]{};

    // 额外元数据
    float headScale = 1.f;  ///< 相对头部大小（用于缩放 3D 特效）
};

// ---------------------------------------------------------------------------
// PnP Solver 回调（可选注入 OpenCV）
// ---------------------------------------------------------------------------
using PnPSolverFn = std::function<bool(
    const std::vector<FaceModelPoint3D>& pts3d,  // 3D 参考点
    const std::vector<std::pair<float,float>>& pts2d, // 对应图像点（像素）
    float fx, float fy, float cx, float cy,       // 相机内参
    float outRvec[3], float outTvec[3]             // 输出旋转向量 + 平移
)>;

// ---------------------------------------------------------------------------
// ArFaceAnchor
// ---------------------------------------------------------------------------
class ArFaceAnchor {
public:
    ArFaceAnchor();
    ~ArFaceAnchor() = default;

    // ── 相机配置 ──────────────────────────────────────────────────────────

    /** 设置图像分辨率（像素）。必须在 estimate() 前调用。 */
    void setImageSize(int width, int height);

    /**
     * 设置相机焦距（像素单位）。
     * 若有 Camera2 真实焦距：fx = fy = physicalFocalMm / sensorWidthMm * imageWidthPx
     * 估算：fx ≈ imageWidth * 1.2  适用于大多数手机
     */
    void setFocalLength(float fx, float fy = 0.f);

    /** 主点（光心），默认为图像中心。 */
    void setPrincipalPoint(float cx, float cy);

    // ── 解算器 ────────────────────────────────────────────────────────────

    /** 注入高精度 PnP solver（如 OpenCV solvePnP）。 */
    void setPnPSolver(PnPSolverFn fn) { m_pnpSolver = std::move(fn); }

    // ── 估算 ──────────────────────────────────────────────────────────────

    /**
     * 从单帧人脸关键点估算 6DOF 位姿。
     * @param face  FaceLandmarkDetector 输出
     * @return      解算结果（invalid 时 valid=false）
     */
    FaceAnchor estimate(const FaceResult& face);

    // ── 投影工具 ──────────────────────────────────────────────────────────

    /**
     * 将 3D 世界坐标投影到图像像素坐标。
     * @param worldPt  3D 点（相机坐标系，cm）
     * @param outPx    输出像素 x
     * @param outPy    输出像素 y
     */
    void projectPoint(const FaceModelPoint3D& worldPt, float& outPx, float& outPy) const;

    /**
     * 构建视图矩阵（相机内参 → GL 投影矩阵），near/far 单位 cm。
     * outMatrix: 列主序 float[16]
     */
    void buildProjectionMatrix(float near, float far, float outMatrix[16]) const;

private:
    int   m_imgW = 1080, m_imgH = 1920;
    float m_fx = 1280.f, m_fy = 1280.f;
    float m_cx = 540.f,  m_cy = 960.f;

    PnPSolverFn m_pnpSolver;

    // 3D 参考点（8点子集：鼻尖、下颌、眼角、嘴角）
    static const FaceModelPoint3D kRefPts3D[8];
    // 对应 106点 landmark 索引
    static const int kRefIdx[8];

    // 内置简化 DLT / EPnP solver
    bool solvePnPInternal(
        const std::vector<FaceModelPoint3D>& pts3d,
        const std::vector<std::pair<float,float>>& pts2d,
        float rvec[3], float tvec[3]) const;

    // Rodrigues 旋转向量 → 旋转矩阵
    static void rodrigues(const float rvec[3], float R[9]);

    // 构建列主序 4×4 模型矩阵
    static void buildModelMatrix(const float R[9], const float tvec[3], float M[16]);

    // 矩阵乘法 C = A × B (4×4 列主序)
    static void matMul4(const float A[16], const float B[16], float C[16]);
};

} // namespace ai
} // namespace video
} // namespace sdk
