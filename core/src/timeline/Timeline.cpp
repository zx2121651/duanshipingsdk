#include "../../include/timeline/Timeline.h"
#include <algorithm>

namespace sdk {
namespace video {
namespace timeline {

Timeline::Timeline(int outputWidth, int outputHeight, int fps)
    : m_outputWidth(outputWidth), m_outputHeight(outputHeight), m_fps(fps) {}

int64_t Timeline::getTotalDuration() const {
    int64_t maxDuration = 0;
    for (const auto& pair : m_tracks) {
        TrackPtr track = pair.second;
        if (!track) continue;

        int64_t trackOut = track->getMaxTimelineOut();
        if (trackOut > maxDuration) {
            maxDuration = trackOut;
        }
    }
    return maxDuration;
}

TrackPtr Timeline::addTrack(int zIndex, Track::TrackType type) {
    if (m_tracks.find(zIndex) == m_tracks.end()) {
        m_tracks[zIndex] = std::make_shared<Track>(zIndex, type);
    }
    return m_tracks[zIndex];
}

void Timeline::removeTrack(int zIndex) {
    m_tracks.erase(zIndex);
}

TrackPtr Timeline::getTrack(int zIndex) const {
    auto it = m_tracks.find(zIndex);
    if (it != m_tracks.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<ClipPtr> Timeline::getActiveVideoClipsAtTime(int64_t timelineUs) const {
    std::vector<ClipPtr> activeClips;

    // m_tracks 是 std::map<int, TrackPtr>，已经按 key (Z-Index) 从小到大排序好了。
    // 这保证了底层轨道（如 z=0, 主轴）排在最前面，高层轨道（如 z=10, 贴纸画中画）在后面，
    // 正好符合 OpenGL Painters Algorithm 从底往顶画，后画的覆盖先画的顺序。
    for (const auto& pair : m_tracks) {
        TrackPtr track = pair.second;
        if (!track) continue;

        if (track->getType() == Track::TrackType::MAIN_VIDEO ||
            track->getType() == Track::TrackType::PIP_VIDEO) {

            ClipPtr clip = track->getActiveClipAtTime(timelineUs);
            if (clip) {
                activeClips.push_back(clip);
            }
        }
    }

    return activeClips;
}

std::vector<ClipPtr> Timeline::getActiveAudioClipsAtTime(int64_t timelineUs) const {
    std::vector<ClipPtr> activeAudio;

    // 对于音频，无论什么轨道类型，只要该轨道没被静音，并且包含了可播放的素材，都要参与混音。
    for (const auto& pair : m_tracks) {
        TrackPtr track = pair.second;
        if (!track || track->getTrackVolume() <= 0.0f) continue;

        ClipPtr clip = track->getActiveClipAtTime(timelineUs);
        if (clip) {
            int64_t relativeUs = timelineUs - clip->getTimelineIn();
            if (clip->getVolume(relativeUs) > 0.0f) {
                activeAudio.push_back(clip);
            }
        }
    }

    return activeAudio;
}

} // namespace timeline
} // namespace video
} // namespace sdk
