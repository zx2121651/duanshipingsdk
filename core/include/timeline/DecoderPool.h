#pragma once
#include "Compositor.h"
#include <string>
#include <map>
#include <mutex>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 基础的视频解码池 (Decoder Pool)
 *
 * 用于在 NLE 架构中按需根据时间戳提取各个素材轨的帧数据。
 * 在 Android 上将对接 AMediaExtractor/AMediaCodec。
 * 在 iOS 上将对接 AVAssetReader。
 */
class DecoderPool : public IDecoderPool {
public:
    DecoderPool();
    ~DecoderPool() override;

    // 核心接口：获取在 localTimeUs 微秒时刻，clipId 对应的视频解码后的 GL 纹理
    Texture getFrame(const std::string& clipId, int64_t localTimeUs) override;

    // 向池中注册一个需要被解码的视频素材
    void registerMedia(const std::string& clipId, const std::string& sourcePath);

    // 释放资源
    void releaseMedia(const std::string& clipId);

private:
    std::mutex m_mutex;

    // 模拟持有的 Decoder 上下文。真实环境下这里会持有平台相关的硬件解码器实例。
    struct DecoderContext {
        std::string sourcePath;
        bool isInitialized = false;
        Texture lastDecodedFrame = {0, 0, 0}; // 缓存的上一帧，用于防抖
        int64_t lastDecodedTimeUs = -1;
    };

    std::map<std::string, std::shared_ptr<DecoderContext>> m_decoders;
};

} // namespace timeline
} // namespace video
} // namespace sdk
