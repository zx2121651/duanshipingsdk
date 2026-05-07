#pragma once
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <algorithm>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// 关键帧插值类型
// ---------------------------------------------------------------------------
enum class InterpolationType {
    LINEAR    = 0, ///< 线性匀速（默认）
    EASE_IN   = 1, ///< 加速进入（慢→快）
    EASE_OUT  = 2, ///< 减速退出（快→慢）
    EASE_IN_OUT=3, ///< 先加速后减速（平滑 S 曲线）
    HOLD      = 4, ///< 保持当前值到下一帧（阶跃/跳切）
};

/** 单个关键帧数据：浮点值 + 出点插值类型 */
struct KeyframeEntry {
    float             value  = 0.f;
    InterpolationType easing = InterpolationType::LINEAR;
};

// ---------------------------------------------------------------------------
// 转场类型
// ---------------------------------------------------------------------------
enum class TransitionType {
    NONE,
    CROSSFADE,
    WIPE_LEFT,
    WIPE_RIGHT,
    WIPE_UP,
    WIPE_DOWN,
    SLIDE_LEFT,
    SLIDE_RIGHT,
    ZOOM_IN,
    FADE_BLACK,
    FLASH
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
    virtual ~Clip() = default;

    std::string getId() const { return m_id; }
    std::string getSourcePath() const { return m_sourcePath; }
    MediaType getType() const { return m_type; }

    // 时间控制
    void setSourceDuration(int64_t durationNs) { m_sourceDuration = durationNs; }
    int64_t getSourceDuration() const { return m_sourceDuration; }

    void setTrimIn(int64_t trimInNs) { m_trimIn = trimInNs; }
    int64_t getTrimIn() const { return m_trimIn; }

    void setTrimOut(int64_t trimOutNs) { m_trimOut = trimOutNs; }
    int64_t getTrimOut() const { return m_trimOut; }

    void setTimelineIn(int64_t timelineInNs) { m_timelineIn = timelineInNs; }
    int64_t getTimelineIn() const { return m_timelineIn; }
    int64_t getTimelineOut() const;

    int64_t getEffectiveTrimIn() const;
    int64_t getEffectiveTrimOut() const;

    // 播放控制
    void setSpeed(float speed) { m_speed = speed; }
    float getSpeed() const { return m_speed; }

    /** @brief 倒放开关。开启时 DecoderPool 将触发精确 Seek 路径（逐帧反向取帧）。 */
    void setReversed(bool reversed) { m_isReversed = reversed; }
    bool isReversed() const { return m_isReversed; }

    // 空间变换
    void setTransform(float scale, float rotation, float transX, float transY);

    // 转场属性 (此 Clip 出现时，与上一层画面的融合方式)
    void setInTransition(TransitionType type, int64_t durationNs) {
        m_inTransitionType = type;
        m_inTransitionDurationNs = durationNs;
        // 同步到注册表 key，方便 Compositor 统一查询
        m_inTransitionName = transitionTypeName(type);
    }
    // 设置来自 TransitionRegistry 的自定义转场（扩展接口）
    void setInTransitionByName(const std::string& name, int64_t durationNs) {
        m_inTransitionType = TransitionType::NONE; // 标记为自定义
        m_inTransitionName = name;
        m_inTransitionDurationNs = durationNs;
    }
    TransitionType getInTransitionType() const { return m_inTransitionType; }
    const std::string& getInTransitionName() const { return m_inTransitionName; }
    int64_t getInTransitionDurationNs() const { return m_inTransitionDurationNs; }

    // 此 Clip 结束时，与下一层画面的融合方式
    void setOutTransition(TransitionType type, int64_t durationNs) {
        m_outTransitionType = type;
        m_outTransitionDurationNs = durationNs;
        m_outTransitionName = transitionTypeName(type);
    }
    void setOutTransitionByName(const std::string& name, int64_t durationNs) {
        m_outTransitionType = TransitionType::NONE;
        m_outTransitionName = name;
        m_outTransitionDurationNs = durationNs;
    }
    TransitionType getOutTransitionType() const { return m_outTransitionType; }
    const std::string& getOutTransitionName() const { return m_outTransitionName; }
    int64_t getOutTransitionDurationNs() const { return m_outTransitionDurationNs; }

    // ── 关键帧控制 ────────────────────────────────────────────────────────────
    // 注意：时间戳是相对于 Clip 的 timelineIn 的相对时间（0 = clip 起点）

    /**
     * 添加或更新一个关键帧。
     * @param easing  该帧到下一帧之间的插值方式（默认线性）
     */
    void addKeyframe(const std::string& paramName, int64_t relativeTimeNs,
                     float value,
                     InterpolationType easing = InterpolationType::LINEAR);

    /** 删除指定参数的指定时间关键帧。 */
    void removeKeyframe(const std::string& paramName, int64_t relativeTimeNs);

    /** 清空某参数的全部关键帧。 */
    void clearKeyframes(const std::string& paramName);

    /**
     * 获取插值后的参数值。
     * 若无关键帧返回 defaultValue；边界外钳位到首/末关键帧值。
     */
    float getInterpolatedParam(const std::string& paramName,
                               int64_t relativeTimeNs,
                               float defaultValue) const;

    /** 获取某参数所有关键帧（有序，按时间戳升序）。 */
    std::vector<std::pair<int64_t, KeyframeEntry>>
    getKeyframes(const std::string& paramName) const;

    /** 参数是否有关键帧。 */
    bool hasKeyframes(const std::string& paramName) const {
        auto it = m_keyframes.find(paramName);
        return it != m_keyframes.end() && !it->second.empty();
    }

    // 快捷方式获取特定属性
    float getOpacity(int64_t relativeTimeNs) const { return getInterpolatedParam("opacity", relativeTimeNs, 1.0f); }
    float getVolume(int64_t relativeTimeNs) const { return getInterpolatedParam("volume", relativeTimeNs, m_volume); }
    float getScale(int64_t relativeTimeNs) const { return getInterpolatedParam("scale", relativeTimeNs, m_scale); }

    void setVolume(float volume) { m_volume = volume; } // Fallback static volume

    // 音调变换（变声）：以半音为单位，0=不变，+12=升一个八度，-12=降一个八度
    void  setPitchShift(float semitones) { m_pitchShiftSemitones = semitones; }
    float getPitchShift() const          { return m_pitchShiftSemitones; }

    // 音量淡入：EASE_OUT 曲线（慢→快→稳定），自动写入 "volume" 关键帧
    void setFadeIn(int64_t durationNs) {
        m_fadeInNs = durationNs;
        addKeyframe("volume", 0,           0.0f, InterpolationType::EASE_OUT);
        addKeyframe("volume", durationNs,  m_volume);
    }
    // 音量淡出：EASE_IN 曲线（稳定→快速衰减）
    void setFadeOut(int64_t startRelNs, int64_t durationNs) {
        m_fadeOutNs = durationNs;
        addKeyframe("volume", startRelNs,              m_volume, InterpolationType::EASE_IN);
        addKeyframe("volume", startRelNs + durationNs, 0.0f);
    }
    int64_t getFadeInDuration()  const { return m_fadeInNs; }
    int64_t getFadeOutDuration() const { return m_fadeOutNs; }

    // 降噪强度 [0, 1]：0=关闭，1=最强（Wiener Filter 调参因子）
    void  setNoiseReduction(float strength) { m_noiseReductionStrength = std::max(0.0f, std::min(1.0f, strength)); }
    float getNoiseReduction() const         { return m_noiseReductionStrength; }

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
    bool  m_isReversed = false;
    float m_pitchShiftSemitones    = 0.0f;
    float m_noiseReductionStrength = 0.0f;
    int64_t m_fadeInNs  = 0;
    int64_t m_fadeOutNs = 0;

    float m_scale = 1.0f;
    float m_rotation = 0.0f;
    float m_transX = 0.0f;
    float m_transY = 0.0f;

    TransitionType m_inTransitionType = TransitionType::NONE;
    std::string    m_inTransitionName;          // TransitionRegistry key
    int64_t        m_inTransitionDurationNs = 0;

    TransitionType m_outTransitionType = TransitionType::NONE;
    std::string    m_outTransitionName;         // TransitionRegistry key
    int64_t        m_outTransitionDurationNs = 0;

    // 将 enum 映射到注册表 key（向后兼容）
    static std::string transitionTypeName(TransitionType t) {
        switch (t) {
            case TransitionType::CROSSFADE:   return "crossfade";
            case TransitionType::WIPE_LEFT:   return "wipe_left";
            case TransitionType::WIPE_RIGHT:  return "wipe_right";
            case TransitionType::WIPE_UP:     return "wipe_up";
            case TransitionType::WIPE_DOWN:   return "wipe_down";
            case TransitionType::SLIDE_LEFT:  return "slide_left";
            case TransitionType::SLIDE_RIGHT: return "slide_right";
            case TransitionType::ZOOM_IN:     return "zoom_in";
            case TransitionType::FADE_BLACK:  return "fade_black";
            case TransitionType::FLASH:       return "flash";
            default:                          return "";
        }
    }

    // 数据结构：属性名 -> (相对时间戳 -> KeyframeEntry{value, easing})
    // std::map 自带按 Key (时间戳) 排序的特性，便于插值查找
    std::map<std::string, std::map<int64_t, KeyframeEntry>> m_keyframes;
};

using ClipPtr = std::shared_ptr<Clip>;

} // namespace timeline
} // namespace video
} // namespace sdk
