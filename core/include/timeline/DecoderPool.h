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
    Texture getFrame(const std::string& clipId, int64_t localTimeNs, bool requiresExactSeek = false);

    // [旧接口兼容] 默认顺序播放
    Texture getFrame(const std::string& clipId, int64_t localTimeNs) override {
        return getFrame(clipId, localTimeNs, false);
    }

    // 向池中注册一个需要被解码的视频素材
    void registerMedia(const std::string& clipId, const std::string& sourcePath);

    // 释放资源
    void releaseMedia(const std::string& clipId);

private:
    std::mutex m_mutex;

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

    void evictDecodersIfNeeded();
};

} // namespace timeline
} // namespace video
} // namespace sdk
