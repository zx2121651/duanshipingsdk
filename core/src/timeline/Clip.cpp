#include "../../include/timeline/Clip.h"

namespace sdk {
namespace video {
namespace timeline {

Clip::Clip(const std::string& id, const std::string& sourcePath, MediaType type)
    : m_id(id), m_sourcePath(sourcePath), m_type(type) {}

int64_t Clip::getTimelineOut() const {
    if (m_speed <= 0.0f) return m_timelineIn;
    // 公式: 主轴结束点 = 放置起始点 + (素材裁切结束点 - 裁切起始点) / 播放倍速
    // 举例：裁切了原视频的 10 秒，按照 2 倍速播放，在主轴上只占 5 秒钟
    int64_t duration = m_trimOut - m_trimIn;
    return m_timelineIn + static_cast<int64_t>(duration / m_speed);
}

void Clip::setTransform(float scale, float rotation, float transX, float transY) {
    m_scale = scale;
    m_rotation = rotation;
    m_transX = transX;
    m_transY = transY;
}

} // namespace timeline
} // namespace video
} // namespace sdk
