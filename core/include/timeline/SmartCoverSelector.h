#pragma once
/**
 * SmartCoverSelector.h
 *
 * 智能封面候选帧选择器。
 *
 * 策略（可组合）：
 *   BRIGHTNESS      亮度最均匀帧（避免过暗/过曝）
 *   SHARPNESS       清晰度最高帧（Laplacian 方差）
 *   FACE_PRESENT    有人脸的帧（需 FaceLandmarkDetector）
 *   MOTION_LOW      运动量最小帧（帧间差分，避免模糊）
 *   TIME_BASED      固定时间点（首帧、1/3、中间等）
 *
 * 用法：
 *   SmartCoverSelector selector;
 *   selector.setStrategy(SmartCoverSelector::Strategy::SHARPNESS |
 *                        SmartCoverSelector::Strategy::FACE_PRESENT);
 *   selector.setCandidateCount(5);
 *   // 依次提交帧
 *   for (auto& frame : videoFrames) {
 *       selector.submitFrame(frame.rgba, frame.width, frame.height, frame.timeNs);
 *   }
 *   auto best = selector.selectBest();
 *   // best.timeNs — 最佳封面帧时间点
 */

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace sdk {
namespace video {
namespace timeline {

struct CoverCandidate {
    int64_t timeNs     = 0;
    float   score      = 0.f;
    float   brightness = 0.f;
    float   sharpness  = 0.f;
    float   motionBlur = 0.f;
    bool    hasFace    = false;

    // 可选：缩略图 RGBA 像素（调用 extractThumbnail 时填充）
    std::vector<uint8_t> thumbnail;
    int thumbWidth  = 0;
    int thumbHeight = 0;
};

class SmartCoverSelector {
public:
    /** 组合策略标志位。 */
    enum Strategy : uint32_t {
        TIME_BASED    = 0,
        BRIGHTNESS    = 1u << 0,
        SHARPNESS     = 1u << 1,
        FACE_PRESENT  = 1u << 2,
        MOTION_LOW    = 1u << 3,
    };

    SmartCoverSelector();
    ~SmartCoverSelector() = default;

    /** 设置启用的评分策略（位组合），默认 BRIGHTNESS | SHARPNESS。 */
    void setStrategy(uint32_t strategy) { m_strategy = strategy; }

    /** 最终返回的候选数量（最多），默认 5。 */
    void setCandidateCount(int n) { m_candidateCount = std::max(1, n); }

    /**
     * 提交一帧用于评估。
     * @param rgba       RGBA 像素数据
     * @param width      帧宽（像素）
     * @param height     帧高（像素）
     * @param timeNs     帧时间戳（相对视频起点，纳秒）
     */
    void submitFrame(const uint8_t* rgba, int width, int height, int64_t timeNs);

    /**
     * 综合评分后选出最佳候选列表。
     * @return 按 score 降序排列的候选列表（最多 candidateCount 个）
     */
    std::vector<CoverCandidate> selectCandidates();

    /** 返回得分最高的单帧候选。 */
    CoverCandidate selectBest();

    /** 清空所有帧记录，重新开始。 */
    void reset() { m_frames.clear(); }

    /** 是否为候选帧生成缩略图（增加 CPU 开销），默认 false。 */
    void setExtractThumbnails(bool v, int thumbW = 192, int thumbH = 108) {
        m_extractThumbs = v;
        m_thumbW = thumbW; m_thumbH = thumbH;
    }

private:
    uint32_t m_strategy       = BRIGHTNESS | SHARPNESS;
    int      m_candidateCount = 5;
    bool     m_extractThumbs  = false;
    int      m_thumbW = 192, m_thumbH = 108;

    struct FrameRecord {
        int64_t timeNs     = 0;
        float   brightness = 0.f;
        float   sharpness  = 0.f;
        float   motion     = 0.f; // inter-frame diff
        bool    hasFace    = false;
        // last frame RGBA for motion calc
        std::vector<uint8_t> pixels;
        int w = 0, h = 0;
    };
    std::vector<FrameRecord> m_frames;
    std::vector<uint8_t>     m_prevPixels;
    int m_prevW = 0, m_prevH = 0;

    // Metrics
    static float computeBrightness(const uint8_t* rgba, int w, int h);
    static float computeSharpness (const uint8_t* rgba, int w, int h);
    static float computeMotion    (const uint8_t* cur, const uint8_t* prev,
                                   int w, int h);
    static void  downsample       (const uint8_t* src, int sw, int sh,
                                   uint8_t* dst, int dw, int dh);
};

} // namespace timeline
} // namespace video
} // namespace sdk
