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

        // 插入后按在主时间轴的起始点 TimelineIn 从小到大排序，方便后续二分查找 Seek。
        // 使用 stable_sort 以确保在 TimelineIn 相同时，维持插入顺序。
        std::stable_sort(m_clips.begin(), m_clips.end(), [](const ClipPtr& a, const ClipPtr& b) {
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

ClipPtr Track::getActiveClipAtTime(int64_t timelineNs) const {
    if (m_clips.empty()) return nullptr;

    // 从后往前遍历，这样在有重叠的情况下，会优先返回“最晚开始”或“最后插入”的片段（即位于上层的片段）。
    for (auto it = m_clips.rbegin(); it != m_clips.rend(); ++it) {
        const auto& clip = *it;
        int64_t start = clip->getTimelineIn();
        int64_t end = clip->getTimelineOut();

        // 游标落入 [TimelineIn, TimelineOut) 区间，则该 Clip 目前应该被渲染/发声
        if (timelineNs >= start && timelineNs < end) {
            return clip;
        }

        // 如果 timelineNs 已经大于等于当前 clip 的结束时间，且由于 m_clips 是按 start 排序的，
        // 我们不能简单的 break，因为前面的 clip 可能还在持续。
        // 但由于我们是从后往前找，找到的第一个符合条件的 [start, end) 就是我们要的“最顶层”片段。
    }

    // 轨道在这个时间点是空的 (比如两段素材之间的黑屏过渡)
    return nullptr;
}

void Track::getActiveClipsAtTime(int64_t timelineNs, std::vector<ClipPtr>& outClips) const {
    for (const auto& clip : m_clips) {
        int64_t start = clip->getTimelineIn();
        int64_t end = clip->getTimelineOut();

        if (timelineNs >= start && timelineNs < end) {
            outClips.push_back(clip);
        } else if (timelineNs < start) {
            // Since clips are sorted by TimelineIn, if we reach a clip that starts
            // after the requested time, we can stop searching.
            break;
        }
    }
}


int64_t Track::getMaxTimelineOut() const {
    int64_t maxOut = 0;
    // 既然我们已经根据 TimelineIn 进行了排序，但考虑到不同 Clip 变速或裁切时长不同，
    // 最晚结束的并不一定就是最后一个 Clip，虽然通常是。为求严谨，遍历取最大值。
    for (const auto& clip : m_clips) {
        int64_t outTime = clip->getTimelineOut();
        if (outTime > maxOut) {
            maxOut = outTime;
        }
    }
    return maxOut;
}


ClipPtr Track::getClip(const std::string& clipId) const {
    for (auto& c : m_clips) {
        if (c->getId() == clipId) {
            return c;
        }
    }
    return nullptr;
}

} // namespace timeline
} // namespace video
} // namespace sdk