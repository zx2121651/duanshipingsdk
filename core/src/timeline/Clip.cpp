#include "../../include/timeline/Clip.h"

namespace sdk {
namespace video {
namespace timeline {

Clip::Clip(const std::string& id, const std::string& sourcePath, MediaType type)
    : m_id(id), m_sourcePath(sourcePath), m_type(type) {}

int64_t Clip::getTimelineOut() const {
    if (m_speed <= 0.0f) return m_timelineIn;
    int64_t duration = m_trimOut - m_trimIn;
    return m_timelineIn + static_cast<int64_t>(duration / m_speed);
}

void Clip::setTransform(float scale, float rotation, float transX, float transY) {
    m_scale = scale;
    m_rotation = rotation;
    m_transX = transX;
    m_transY = transY;
}

void Clip::addKeyframe(const std::string& paramName, int64_t relativeTimeNs, float value) {
    m_keyframes[paramName][relativeTimeNs] = value;
}

float Clip::getInterpolatedParam(const std::string& paramName, int64_t relativeTimeNs, float defaultValue) const {
    auto itParam = m_keyframes.find(paramName);
    if (itParam == m_keyframes.end() || itParam->second.empty()) {
        return defaultValue; // No keyframes for this param
    }

    const auto& timeMap = itParam->second;

    // upper_bound finds the first element whose key is > relativeTimeNs
    auto itNext = timeMap.upper_bound(relativeTimeNs);

    if (itNext == timeMap.begin()) {
        // Requested time is before the first keyframe, return the first value
        return itNext->second;
    }

    if (itNext == timeMap.end()) {
        // Requested time is after the last keyframe, return the last value
        auto itLast = timeMap.rbegin();
        return itLast->second;
    }

    // Interpolate between itPrev and itNext
    auto itPrev = itNext;
    --itPrev;

    int64_t t0 = itPrev->first;
    float v0 = itPrev->second;
    int64_t t1 = itNext->first;
    float v1 = itNext->second;

    float ratio = static_cast<float>(relativeTimeNs - t0) / static_cast<float>(t1 - t0);
    return v0 + (v1 - v0) * ratio;
}

} // namespace timeline
} // namespace video
} // namespace sdk
