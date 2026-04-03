#pragma once
#include "Track.h"
#include <vector>
#include <map>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 时间线引擎总控 (Timeline NLE Engine)
 * 管理工程级 (Project Level) 参数，例如画布比例、分辨率、帧率，以及所有图层(Track)。
 * 它是整个视频剪辑的数据单例来源，指导底层多线程 Decoder 和 Compositor 工作。
 */
class Timeline {
public:
    Timeline(int outputWidth, int outputHeight, int fps);
    ~Timeline() = default;

    int getOutputWidth() const { return m_outputWidth; }
    int getOutputHeight() const { return m_outputHeight; }
    int getFps() const { return m_fps; }

    // ------------------------------------------------------------------------
    // 全局工程时长 (Project Duration)
    // ------------------------------------------------------------------------

    // 由所有轨道中最长那个 Clip 的 TimelineOut 决定
    int64_t getTotalDuration() const;

    // ------------------------------------------------------------------------
    // 轨道调度管理 (Track Management)
    // ------------------------------------------------------------------------

    // 创建一个新的图层轨 (按 zIndex 排序叠加)
    TrackPtr addTrack(int zIndex, Track::TrackType type);

    void removeTrack(int zIndex);

    TrackPtr getTrack(int zIndex) const;

    // 获取当前时间点 (Timeline T) 应该渲染的所有素材（按 Z-Index 从底到顶排列）
    // 这个接口将直接喂给离屏合成器 (Offscreen Compositor) 进行多 FBO 混合
    std::vector<ClipPtr> getActiveVideoClipsAtTime(int64_t timelineUs) const;

    // 同理，获取当前时间点所有需要混音 (Audio Mixing) 的素材
    std::vector<ClipPtr> getActiveAudioClipsAtTime(int64_t timelineUs) const;

private:
    int m_outputWidth;
    int m_outputHeight;
    int m_fps;

    std::map<int, TrackPtr> m_tracks;
};

using TimelinePtr = std::shared_ptr<Timeline>;

} // namespace timeline
} // namespace video
} // namespace sdk
