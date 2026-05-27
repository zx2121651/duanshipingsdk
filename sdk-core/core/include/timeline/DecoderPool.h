#pragma once
#include "Compositor.h"
#include "VideoDecoder.h"
#include <string>
#include <map>
#include <mutex>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 软解 Fallback 策略
 *
 *  AUTO     — 硬件解码失败时自动切换软解（默认）
 *  HW_ONLY  — 仅硬件解码；失败则返回错误帧（保帧优先）
 *  SW_FIRST — 优先软解，主要用于调试或低端机
 */
enum class DecoderFallbackStrategy {
    AUTO     = 0,
    HW_ONLY  = 1,
    SW_FIRST = 2,
};

/**
 * @brief 视频解码池 (Decoder Pool)
 *
 * 用于在 NLE 架构中按需根据时间戳提取各个素材轨的帧数据。
 * 在 Android 上对接 AMediaExtractor/AMediaCodec。
 * 在 iOS 上对接 AVAssetReader。
 */
class DecoderPool : public IDecoderPool {
public:
    DecoderPool();
    ~DecoderPool() override;

    // 核心接口：获取在 localTimeNs 微秒时刻，clipId 对应的视频解码后的 GL 纹理
    // requiresExactSeek: 如果为 true，表示这不是按顺序播放，而是用户在拖拽游标或者倒放，此时需要精准 Seek
    ResultPayload<Texture> getFrame(const std::string& clipId, int64_t localTimeNs, bool requiresExactSeek = false);

    // [旧接口兼容] 默认顺序播放
    ResultPayload<Texture> getFrame(const std::string& clipId, int64_t localTimeNs) override {
        return getFrame(clipId, localTimeNs, false);
    }

    // 向池中注册一个需要被解码的视频素材
    void registerMedia(const std::string& clipId, const std::string& sourcePath);

    // 释放资源
    void releaseMedia(const std::string& clipId);

    // 清除所有注册的资源
    void clear();

    /**
     * @brief P1-3: Hint the pool to start decoding ahead to the given time.
     * Must be called from the render thread after getFrame(); non-blocking.
     * The pool marks the context so decodeLoop() fills its queue for timeNs+1frame.
     */
    void prefetchFrame(const std::string& clipId, int64_t upcomingTimeNs);

    /**
     * @brief 设置软解 Fallback 策略。
     *
     * 默认 AUTO：AMediaCodec 失败后自动尝试 FFmpeg 软解。
     * SW_FIRST：直接走软解（适合低端机、模拟器）。
     * HW_ONLY：不允许软解降级（适合对帧质量严格的导出场景）。
     *
     * @note 线程安全。可在任意线程调用（下次 getFrame() 时生效）。
     */
    void setFallbackStrategy(DecoderFallbackStrategy strategy) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_strategy = strategy;
    }

    DecoderFallbackStrategy getFallbackStrategy() const {
        return m_strategy;
    }

    /**
     * @brief 精确帧 seek 模式开关（用于 scrub/seek 场景）。
     *
     * 开启后，下一次 getFrame() 调用将自动设置 requiresExactSeek=true，
     * 并在返回后自动关闭（单次触发语义），避免影响正常播放性能。
     *
     * 线程安全。
     */
    void setExactSeekMode(bool enable) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_exactSeekMode = enable;
    }
    bool isExactSeekMode() const { return m_exactSeekMode; }

private:
    std::recursive_mutex m_mutex;

    // 核心限制：防止多轨画中画导致 OOM/Jetsam
    static constexpr int MAX_CONCURRENT_DECODERS = 4;

    struct DecoderContext {
        std::string sourcePath;
        std::shared_ptr<VideoDecoder> decoder; // 硬件解码器
        std::shared_ptr<VideoDecoder> softDecoder; // 软件解码器兜底
        bool hwFailed = false; // 如果硬件彻底报废，标记此项
        bool isInitialized = false;
        Texture lastDecodedFrame = {0, 0, 0};
        int64_t lastDecodedTimeNs = -1;
        uint64_t lastAccessCounter = 0;
    };

    std::map<std::string, std::shared_ptr<DecoderContext>> m_decoders;
    uint64_t m_accessCounter = 0;
    int m_activeDecoderCount = 0;
    DecoderFallbackStrategy m_strategy = DecoderFallbackStrategy::AUTO;
    bool m_exactSeekMode = false;

    void evictDecodersIfNeeded();
};

} // namespace timeline
} // namespace video
} // namespace sdk
