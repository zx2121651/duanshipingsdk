#pragma once
#include <string>
#include <memory>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 媒体片段 (Clip)
 * 剪辑引擎的最小物理单元，代表用户拖入时间线的一段视频、图片或音频。
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

    // 唯一标识符，用于 UI 层对应
    std::string getId() const { return m_id; }
    std::string getSourcePath() const { return m_sourcePath; }
    MediaType getType() const { return m_type; }

    // ------------------------------------------------------------------------
    // 时间控制 (Time Control) - 极其重要：所有时间单位统一使用微秒 (Microseconds)
    // ------------------------------------------------------------------------

    // 素材本身的物理时长 (由底层解码器探活后填入)
    void setSourceDuration(int64_t durationUs) { m_sourceDuration = durationUs; }
    int64_t getSourceDuration() const { return m_sourceDuration; }

    // 用户在素材上裁切的起始点 (Trim In)
    void setTrimIn(int64_t trimInUs) { m_trimIn = trimInUs; }
    int64_t getTrimIn() const { return m_trimIn; }

    // 用户在素材上裁切的结束点 (Trim Out)
    void setTrimOut(int64_t trimOutUs) { m_trimOut = trimOutUs; }
    int64_t getTrimOut() const { return m_trimOut; }

    // 这段裁切好的素材，被放置在主时间线 (Timeline) 上的起始位置
    void setTimelineIn(int64_t timelineInUs) { m_timelineIn = timelineInUs; }
    int64_t getTimelineIn() const { return m_timelineIn; }

    // 在时间线上的结束位置 (通常 = TimelineIn + (TrimOut - TrimIn) / Speed)
    int64_t getTimelineOut() const;

    // ------------------------------------------------------------------------
    // 播放控制 (Playback Control)
    // ------------------------------------------------------------------------

    // 变速系数 (例如 0.5=慢放两倍，2.0=快放两倍)
    void setSpeed(float speed) { m_speed = speed; }
    float getSpeed() const { return m_speed; }

    // 音量系数 (0.0=静音，1.0=原声，2.0=放大两倍)
    void setVolume(float volume) { m_volume = volume; }
    float getVolume() const { return m_volume; }

    // ------------------------------------------------------------------------
    // 空间变换控制 (Spatial Control) - 针对画中画或视频旋转缩放
    // ------------------------------------------------------------------------

    // 基础变换 (0.0~1.0 的归一化坐标系)
    void setTransform(float scale, float rotation, float transX, float transY);

    // TODO: 获取矩阵 (用于 OpenGL 渲染时传入 Uniform)
    // std::vector<float> getTransformMatrix() const;

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

    // 空间属性
    float m_scale = 1.0f;
    float m_rotation = 0.0f;
    float m_transX = 0.0f;
    float m_transY = 0.0f;
};

using ClipPtr = std::shared_ptr<Clip>;

} // namespace timeline
} // namespace video
} // namespace sdk
