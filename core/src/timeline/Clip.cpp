#include "../../include/timeline/Clip.h"
#include <algorithm>
#include <cmath>

namespace sdk {
namespace video {
namespace timeline {

Clip::Clip(const std::string& id, const std::string& sourcePath, MediaType type)
    : m_id(id), m_sourcePath(sourcePath), m_type(type) {}

int64_t Clip::getTimelineOut() const {
    if (m_speed <= 0.00001f) return m_timelineIn; // Protect against speed <= 0 and near-zero
    int64_t duration = getEffectiveTrimOut() - getEffectiveTrimIn();
    return m_timelineIn + static_cast<int64_t>(std::round(duration / static_cast<long double>(m_speed)));
}

int64_t Clip::getEffectiveTrimIn() const {
    int64_t trimIn = std::max(static_cast<int64_t>(0), m_trimIn);
    return std::min(trimIn, getEffectiveTrimOut());
}

int64_t Clip::getEffectiveTrimOut() const {
    int64_t effectiveOut = (m_trimOut > 0) ? m_trimOut : m_sourceDuration;
    return std::clamp(effectiveOut, static_cast<int64_t>(0), m_sourceDuration);
}

void Clip::setTransform(float scale, float rotation, float transX, float transY) {
    m_scale = scale;
    m_rotation = rotation;
    m_transX = transX;
    m_transY = transY;
}

// ---------------------------------------------------------------------------
// 缓动曲线辅助函数
// ---------------------------------------------------------------------------
float Clip::applyEasing(float t, const KeyframeEntry& entry) {
    switch (entry.easing) {
        case InterpolationType::EASE_IN:
            return t * t;                                     // quadratic ease-in
        case InterpolationType::EASE_OUT:
            return 1.f - (1.f - t) * (1.f - t);              // quadratic ease-out
        case InterpolationType::EASE_IN_OUT:
            return t < 0.5f
                ? 2.f * t * t
                : 1.f - 2.f * (1.f - t) * (1.f - t);        // smooth S-curve
        case InterpolationType::HOLD:
            return 0.f;                                       // never reaches 1 (step at next KF)
        case InterpolationType::BEZIER:
            return Clip::evaluateBezier(t, entry.cp1x, entry.cp1y, entry.cp2x, entry.cp2y);
        default:
            return t;                                         // LINEAR
    }
}

void Clip::addKeyframe(const std::string& paramName, int64_t relativeTimeNs,
                       float value, InterpolationType easing) {
    m_keyframes[paramName][relativeTimeNs] = KeyframeEntry{value, easing};
}

void Clip::addKeyframe(const std::string& paramName, int64_t relativeTimeNs,
                       float value, float cp1x, float cp1y, float cp2x, float cp2y) {
    KeyframeEntry entry;
    entry.value = value;
    entry.easing = InterpolationType::BEZIER;
    entry.cp1x = cp1x;
    entry.cp1y = cp1y;
    entry.cp2x = cp2x;
    entry.cp2y = cp2y;
    m_keyframes[paramName][relativeTimeNs] = entry;
}

void Clip::removeKeyframe(const std::string& paramName, int64_t relativeTimeNs) {
    auto it = m_keyframes.find(paramName);
    if (it != m_keyframes.end()) it->second.erase(relativeTimeNs);
}

void Clip::clearKeyframes(const std::string& paramName) {
    m_keyframes.erase(paramName);
}

std::vector<std::pair<int64_t, KeyframeEntry>>
Clip::getKeyframes(const std::string& paramName) const {
    auto it = m_keyframes.find(paramName);
    if (it == m_keyframes.end()) return {};
    return { it->second.begin(), it->second.end() };
}

float Clip::getInterpolatedParam(const std::string& paramName,
                                  int64_t relativeTimeNs,
                                  float defaultValue) const {
    auto itParam = m_keyframes.find(paramName);
    if (itParam == m_keyframes.end() || itParam->second.empty())
        return defaultValue;

    const auto& timeMap = itParam->second;
    auto itNext = timeMap.upper_bound(relativeTimeNs);

    if (itNext == timeMap.begin())
        return itNext->second.value;    // before first keyframe

    if (itNext == timeMap.end())
        return timeMap.rbegin()->second.value;  // after last keyframe

    auto itPrev = std::prev(itNext);
    int64_t t0 = itPrev->first;
    int64_t t1 = itNext->first;
    float   v0 = itPrev->second.value;
    float   v1 = itNext->second.value;

    float ratio = static_cast<float>(relativeTimeNs - t0)
                / static_cast<float>(t1 - t0);
    float easedRatio = applyEasing(ratio, itPrev->second);
    return v0 + (v1 - v0) * easedRatio;
}

float Clip::evaluateBezier(float t, float cp1x, float cp1y, float cp2x, float cp2y) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    // Cubic Bezier: x(t) = 3(1-t)^2*t*cp1x + 3(1-t)t^2*cp2x + t^3
    // Simplified for easing (P0=0,0; P3=1,1):
    // x(t) = (3*cp1x - 3*cp2x + 1)t^3 + (3*cp2x - 6*cp1x)t^2 + (3*cp1x)t

    auto getX = [&](float t) {
        return ((3.0f * cp1x - 3.0f * cp2x + 1.0f) * t * t * t) +
               ((3.0f * cp2x - 6.0f * cp1x) * t * t) +
               (3.0f * cp1x * t);
    };

    auto getSlope = [&](float t) {
        return (3.0f * (3.0f * cp1x - 3.0f * cp2x + 1.0f) * t * t) +
               (2.0f * (3.0f * cp2x - 6.0f * cp1x) * t) +
               (3.0f * cp1x);
    };

    // Newton's method to solve for t (finding t such that getX(t) = target_x)
    float targetX = t;
    float currentT = targetX;
    for (int i = 0; i < 8; ++i) {
        float currentX = getX(currentT) - targetX;
        float slope = getSlope(currentT);
        if (std::abs(currentX) < 0.001f || std::abs(slope) < 1e-6f) break;
        currentT -= currentX / slope;
    }

    // Now compute y for that t:
    // y(t) = (3*cp1y - 3*cp2y + 1)t^3 + (3*cp2y - 6*cp1y)t^2 + (3*cp1y)t
    return ((3.0f * cp1y - 3.0f * cp2y + 1.0f) * currentT * currentT * currentT) +
           ((3.0f * cp2y - 6.0f * cp1y) * currentT * currentT) +
           (3.0f * cp1y * currentT);
}

// ---------------------------------------------------------------------------
// 时间重映射 (Time Remapping)
// ---------------------------------------------------------------------------
void Clip::setSpeedCurvePoint(int64_t relativeTimeNs, float speed) {
    m_speedCurve[relativeTimeNs] = speed;
}

float Clip::getSpeedAtTime(int64_t relativeTimeNs) const {
    if (m_speedCurve.empty()) return m_speed;

    auto itNext = m_speedCurve.lower_bound(relativeTimeNs);
    if (itNext == m_speedCurve.begin()) return itNext->second;
    if (itNext == m_speedCurve.end()) return std::prev(itNext)->second;

    auto itPrev = std::prev(itNext);
    int64_t t0 = itPrev->first;
    int64_t t1 = itNext->first;
    float   v0 = itPrev->second;
    float   v1 = itNext->second;

    float ratio = static_cast<float>(relativeTimeNs - t0) / static_cast<float>(t1 - t0);
    return v0 + (v1 - v0) * ratio;
}

int64_t Clip::getRemappedTime(int64_t relativeTimeNs) const {
    if (m_speedCurve.empty()) {
        return static_cast<int64_t>(std::round(relativeTimeNs * static_cast<long double>(m_speed)));
    }

    long double totalSourceNs = 0;
    int64_t lastT = 0;

    for (auto const& [t, v] : m_speedCurve) {
        if (t <= 0) {
            lastT = t;
            continue;
        }
        if (t > relativeTimeNs) break;

        int64_t t0 = lastT;
        int64_t t1 = t;
        float   v0 = getSpeedAtTime(t0);
        float   v1 = v;

        // Integral of linear speed: v(u) = v0 + (v1-v0)*(u-t0)/(t1-t0)
        // SourceTime = Integral(v(u) du) from t0 to t1 = (v0 + v1) / 2 * (t1 - t0)
        totalSourceNs += static_cast<long double>(v0 + v1) * 0.5 * (t1 - t0);
        lastT = t1;
    }

    if (lastT < relativeTimeNs) {
        float v0 = getSpeedAtTime(lastT);
        float v1 = getSpeedAtTime(relativeTimeNs);
        totalSourceNs += static_cast<long double>(v0 + v1) * 0.5 * (relativeTimeNs - lastT);
    }

    return static_cast<int64_t>(std::round(totalSourceNs));
}

// ---------------------------------------------------------------------------
// EffectClip
// ---------------------------------------------------------------------------
EffectClip::EffectClip(const std::string& id, const std::string& effectType)
    : Clip(id, "", MediaType::EFFECT), m_effectType(effectType) {}

} // namespace timeline
} // namespace video
} // namespace sdk
