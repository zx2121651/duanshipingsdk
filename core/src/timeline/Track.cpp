#include "../../include/timeline/Track.h"
#include <algorithm>

namespace sdk {
namespace video {
namespace timeline {

Track::Track(int zIndex, TrackType type) : m_zIndex(zIndex), m_type(type) {}

void Track::addClip(ClipPtr clip) {
    if (clip) {
        // 在实际剪辑器里，这里还需要检查新插入的 Clip 是否与现有的 Clip 在主轴上时间段发生冲突 (Overlap)
        // 并根据需求进行“吸附”、“覆盖”或“整体后移”逻辑。这里做简化插入。
        m_clips.push_back(clip);

        // 插入后按在主时间轴的起始点 TimelineIn 从小到大排序，方便后续二分查找 Seek
        std::sort(m_clips.begin(), m_clips.end(), [](const ClipPtr& a, const ClipPtr& b) {
            return a->getTimelineIn() < b->getTimelineIn();
        });
    }
}

void Track::removeClip(const std::string& clipId) {
    m_clips.erase(
        std::remove_if(m_clips.begin(), m_clips.end(), [&](const ClipPtr& clip) {
            return clip->getId() == clipId;
        }),
        m_clips.end()
    );
}

void Track::clearClips() {
    m_clips.clear();
}

ClipPtr Track::getActiveClipAtTime(int64_t timelineUs) const {
    if (m_clips.empty()) return nullptr;

    // 因为 m_clips 是按 TimelineIn 有序的，可以用二分查找 (std::lower_bound) 优化，
    // 这里为了演示 NLE 逻辑，直接线性遍历
    for (const auto& clip : m_clips) {
        int64_t start = clip->getTimelineIn();
        int64_t end = clip->getTimelineOut();

        // 游标落入 [TimelineIn, TimelineOut) 区间，则该 Clip 目前应该被渲染/发声
        if (timelineUs >= start && timelineUs < end) {
            return clip;
        }

        // 如果游标早于当前 Clip，说明后面的都无需找了（因为序列是有序的）
        if (timelineUs < start) break;
    }

    // 轨道在这个时间点是空的 (比如两段素材之间的黑屏过渡)
    return nullptr;
}

} // namespace timeline
} // namespace video
} // namespace sdk
