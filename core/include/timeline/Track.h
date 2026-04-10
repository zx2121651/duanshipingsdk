#pragma once
#include "Clip.h"
#include <vector>
#include <memory>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 轨道 (Track)
 * 代表剪辑工具中的一整层视频、画中画(PiP)、音频或贴纸层。
 * 它包含了一个或多个首尾不重叠的 Clip。
 */
class Track {
public:
    enum class TrackType {
        MAIN_VIDEO, // 主视频轴 (决定主画幅和转场基准)
        PIP_VIDEO,  // 画中画层 (可以在空间上自由缩放)
        AUDIO_ONLY  // 纯音频轨 (无画面)
    };

    Track(int zIndex, TrackType type);
    ~Track() = default;

    int getZIndex() const { return m_zIndex; }
    TrackType getType() const { return m_type; }

    // ------------------------------------------------------------------------
    // Clip 管理 (Clip Management)
    // ------------------------------------------------------------------------

    // 向轨道中添加一个片段
    void addClip(ClipPtr clip);

    // 移除指定 ID 的片段
    void removeClip(const std::string& clipId);
    ClipPtr getClip(const std::string& clipId) const;

    // 清空轨道
    void clearClips();

    // ------------------------------------------------------------------------
    // 播放游标寻址 (Seek and Playback)
    // 根据主时间线的当前时间 T，找出该轨道上在这个时刻唯一可见/发声的 Clip
    // ------------------------------------------------------------------------
    ClipPtr getActiveClipAtTime(int64_t timelineNs) const;

    // ------------------------------------------------------------------------
    // 混合属性 (Blending Properties)
    // ------------------------------------------------------------------------
    void setOpacity(float opacity) { m_opacity = opacity; }
    float getOpacity() const { return m_opacity; }

    // 针对音频轨，也可以设置整条轨道的基准音量
    void setTrackVolume(float volume) { m_trackVolume = volume; }
    float getTrackVolume() const { return m_trackVolume; }

    int64_t getMaxTimelineOut() const;

private:
    int m_zIndex;
    TrackType m_type;

    std::vector<ClipPtr> m_clips;

    float m_opacity = 1.0f;
    float m_trackVolume = 1.0f;
};

using TrackPtr = std::shared_ptr<Track>;

} // namespace timeline
} // namespace video
} // namespace sdk
