#pragma once
#include <string>
#include <memory>
#include <map>

namespace sdk {
namespace video {
namespace timeline {

// 转场类型
enum class TransitionType {
    NONE,
    CROSSFADE,
    WIPE_LEFT
};

/**
 * @brief 媒体片段 (Clip)
 */
class Clip {
public:
    enum class MediaType {
        VIDEO,
        AUDIO,
        IMAGE
    };

    Clip(const std::string& id, const std::string& sourcePath, MediaType type);
    ~Clip() = default;

    std::string getId() const { return m_id; }
    std::string getSourcePath() const { return m_sourcePath; }
    MediaType getType() const { return m_type; }

    // 时间控制
    void setSourceDuration(int64_t durationUs) { m_sourceDuration = durationUs; }
    int64_t getSourceDuration() const { return m_sourceDuration; }

    void setTrimIn(int64_t trimInUs) { m_trimIn = trimInUs; }
    int64_t getTrimIn() const { return m_trimIn; }

    void setTrimOut(int64_t trimOutUs) { m_trimOut = trimOutUs; }
    int64_t getTrimOut() const { return m_trimOut; }

    void setTimelineIn(int64_t timelineInUs) { m_timelineIn = timelineInUs; }
    int64_t getTimelineIn() const { return m_timelineIn; }
    int64_t getTimelineOut() const;

    // 播放控制
    void setSpeed(float speed) { m_speed = speed; }
    float getSpeed() const { return m_speed; }

    // 空间变换
    void setTransform(float scale, float rotation, float transX, float transY);

    // 转场属性 (此 Clip 出现时，与上一层画面的融合方式)
    void setInTransition(TransitionType type, int64_t durationUs) {
        m_inTransitionType = type;
        m_inTransitionDurationUs = durationUs;
    }
    TransitionType getInTransitionType() const { return m_inTransitionType; }
    int64_t getInTransitionDurationUs() const { return m_inTransitionDurationUs; }

    // 关键帧控制
    // 注意: 时间戳是相对于 Clip 的 timelineIn 而言的相对时间 (0 开始)
    void addKeyframe(const std::string& paramName, int64_t relativeTimeUs, float value);

    // 获取插值后的参数。如果没有关键帧，返回 defaultValue
    float getInterpolatedParam(const std::string& paramName, int64_t relativeTimeUs, float defaultValue) const;

    // 快捷方式获取特定属性
    float getOpacity(int64_t relativeTimeUs) const { return getInterpolatedParam("opacity", relativeTimeUs, 1.0f); }
    float getVolume(int64_t relativeTimeUs) const { return getInterpolatedParam("volume", relativeTimeUs, m_volume); }
    float getScale(int64_t relativeTimeUs) const { return getInterpolatedParam("scale", relativeTimeUs, m_scale); }

    void setVolume(float volume) { m_volume = volume; } // Fallback static volume

private:
    std::string m_id;
    std::string m_sourcePath;
    MediaType m_type;

    int64_t m_sourceDuration = 0;
    int64_t m_trimIn = 0;
    int64_t m_trimOut = 0;
    int64_t m_timelineIn = 0;

    float m_speed = 1.0f;
    float m_volume = 1.0f;

    float m_scale = 1.0f;
    float m_rotation = 0.0f;
    float m_transX = 0.0f;
    float m_transY = 0.0f;

    TransitionType m_inTransitionType = TransitionType::NONE;
    int64_t m_inTransitionDurationUs = 0;

    // 数据结构：属性名 -> (相对时间戳 -> 参数值)
    // std::map 自带按 Key (时间戳) 排序的特性，极大方便插值查找
    std::map<std::string, std::map<int64_t, float>> m_keyframes;
};

using ClipPtr = std::shared_ptr<Clip>;

} // namespace timeline
} // namespace video
} // namespace sdk
