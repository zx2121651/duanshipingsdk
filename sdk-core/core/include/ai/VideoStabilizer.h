#pragma once
/**
 * VideoStabilizer.h
 *
 * 视频防抖接口（支持两种后端策略）。
 *
 * 策略：
 *   GYRO          使用陀螺仪数据（低延迟，拍摄端实时稳定）
 *   OPTICAL_FLOW  使用帧间光流（后期处理，精度高，需完整视频）
 *   AUTO          优先 GYRO，无陀螺仪数据则回退 OPTICAL_FLOW
 *
 * 用法A（后期导出防抖）：
 *   VideoStabilizer stabilizer;
 *   stabilizer.setStrategy(VideoStabilizer::Strategy::OPTICAL_FLOW);
 *   stabilizer.setSmoothRadius(30);
 *   // 第一遍：分析全部帧获取运动轨迹
 *   for (auto& frame : frames) {
 *       stabilizer.analyzeFrame(frame.rgba, frame.width, frame.height, frame.timeNs);
 *   }
 *   stabilizer.computeTrajectory();
 *   // 第二遍：应用稳定变换
 *   for (auto& frame : frames) {
 *       StabilizedTransform t = stabilizer.getTransformAt(frame.timeNs);
 *       applyAffineTransform(frame, t);
 *   }
 *
 * 用法B（实时拍摄防抖）：
 *   stabilizer.setStrategy(VideoStabilizer::Strategy::GYRO);
 *   stabilizer.addGyroSample({timestampNs, gx, gy, gz});
 *   StabilizedTransform t = stabilizer.getTransformAt(currentTimeNs);
 */

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <functional>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// 2D 仿射稳定变换（平移 + 旋转 + 缩放）
// ---------------------------------------------------------------------------
struct StabilizedTransform {
    float dx         = 0.f;  ///< X 平移（像素，相对中心）
    float dy         = 0.f;  ///< Y 平移
    float rotation   = 0.f;  ///< 旋转角度（弧度）
    float scale      = 1.f;  ///< 均匀缩放（裁边补偿用，通常 1.0–1.1）
    bool  valid      = true;
};

// ---------------------------------------------------------------------------
// 陀螺仪采样
// ---------------------------------------------------------------------------
struct GyroSample {
    int64_t timestampNs = 0;
    float   gx = 0.f;  ///< 角速度 X（rad/s）
    float   gy = 0.f;
    float   gz = 0.f;
};

// ---------------------------------------------------------------------------
// VideoStabilizer
// ---------------------------------------------------------------------------
class VideoStabilizer {
public:
    enum class Strategy { GYRO, OPTICAL_FLOW, AUTO };

    VideoStabilizer();
    ~VideoStabilizer() = default;

    // ── 配置 ──────────────────────────────────────────────────────────────

    void setStrategy(Strategy s) { m_strategy = s; }

    /**
     * 平滑半径（帧数），默认 30。
     * 更大的值 = 更平滑但裁边更多。
     */
    void setSmoothRadius(int r) { m_smoothRadius = std::max(1, r); }

    /**
     * 裁边比例 [0, 0.2]，默认 0.05（5%）。
     * 缩放补偿边缘空白，略微裁切画面。
     */
    void setCropRatio(float r) { m_cropRatio = r; }

    /** 最大允许修正量（像素），防止大幅度抖动被过度修正。 */
    void setMaxCorrectionPx(float px) { m_maxCorrectionPx = px; }

    // ── 陀螺仪路径 ────────────────────────────────────────────────────────

    /** 追加一个陀螺仪采样（实时调用）。 */
    void addGyroSample(const GyroSample& sample);

    /** 批量设置陀螺仪数据（导出时使用）。 */
    void setGyroSamples(std::vector<GyroSample> samples);

    // ── 光流路径 ──────────────────────────────────────────────────────────

    /**
     * 分析一帧（第一遍扫描）。
     * 内部计算与上一帧的光流/特征点匹配，积累运动轨迹。
     */
    void analyzeFrame(const uint8_t* rgba, int width, int height, int64_t timeNs);

    /**
     * 完成分析后调用，计算平滑后的目标轨迹。
     * analyzeFrame 全部调用完毕后调用一次。
     */
    void computeTrajectory();

    // ── 查询 ──────────────────────────────────────────────────────────────

    /**
     * 查询某时间点的稳定变换。
     * @param timeNs  视频内时间戳（纳秒）
     * @return        需应用到该帧的仿射变换
     */
    StabilizedTransform getTransformAt(int64_t timeNs) const;

    /** 重置所有状态。 */
    void reset();

    /** 已分析帧数。 */
    int analyzedFrameCount() const { return (int)m_rawMotions.size(); }

private:
    Strategy m_strategy         = Strategy::AUTO;
    int      m_smoothRadius     = 30;
    float    m_cropRatio        = 0.05f;
    float    m_maxCorrectionPx  = 40.f;

    // Per-frame motion record
    struct MotionRecord {
        int64_t timeNs = 0;
        float   dx     = 0.f;
        float   dy     = 0.f;
        float   angle  = 0.f;
    };

    std::vector<MotionRecord> m_rawMotions;      ///< 原始帧间运动
    std::vector<MotionRecord> m_smoothedMotions; ///< 平滑后运动轨迹
    std::vector<GyroSample>   m_gyroSamples;

    // Previous frame (grayscale, downsampled) for optical flow
    std::vector<uint8_t> m_prevGray;
    int m_prevW = 0, m_prevH = 0;

    // ── 光流辅助 ─────────────────────────────────────────────────────────
    static void rgbaToGray(const uint8_t* rgba, int w, int h,
                           uint8_t* gray);
    static void downsample(const uint8_t* src, int sw, int sh,
                           uint8_t* dst, int dw, int dh);

    /** 简化 Lucas-Kanade 单点块匹配（不依赖 OpenCV）。 */
    static bool matchBlock(const uint8_t* prev, const uint8_t* cur,
                           int w, int h,
                           int px, int py,   // 原点位置
                           float& outDx, float& outDy);

    // 高斯平滑运动轨迹
    std::vector<float> gaussSmooth(const std::vector<float>& signal,
                                   int radius) const;

    // 陀螺仪积分得到相对偏移
    StabilizedTransform integrateGyro(int64_t timeNs) const;
};

} // namespace ai
} // namespace video
} // namespace sdk
