#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../GLTypes.h" // For Result

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 异步音频解码器接口
 * 保证输出统一为 44.1kHz, 16-bit, Stereo (双声道交错 PCM)
 */
class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;

    virtual Result open(const std::string& filePath) = 0;

    /**
     * @brief 请求指定时间段的 PCM 数据
     * @param timeNs  请求的起始相对时间（纳秒）
     * @param durationNs 请求的持续时间（纳秒）
     * @return 16-bit PCM 样本数组（交错双声道: L R L R...），采样率由 getSourceSampleRate() 决定
     */
    virtual std::vector<int16_t> getPcmData(int64_t timeNs, int64_t durationNs) = 0;

    /**
     * @brief 返回该解码器输出的原始采样率（Hz），如 44100 / 48000 / 22050。
     * AudioMixer 据此决定是否执行重采样至 44100 Hz。
     */
    virtual int getSourceSampleRate() const { return 44100; }

    virtual void close() = 0;
};

class AudioDecoderPool {
public:
    virtual ~AudioDecoderPool() = default;

    virtual void registerMedia(const std::string& clipId, const std::string& sourcePath) = 0;
    virtual void releaseMedia(const std::string& clipId) = 0;

    virtual std::vector<int16_t> getPcmData(const std::string& clipId, int64_t localTimeNs, int64_t durationNs) = 0;

    /**
     * @brief 返回指定素材的原始采样率（Hz）。
     * 默认返回 44100，具体实现应查询对应的 AudioDecoder。
     */
    virtual int getSourceSampleRate(const std::string& clipId) const { return 44100; }
};

using AudioDecoderPoolPtr = std::shared_ptr<AudioDecoderPool>;

/**
 * @brief Platform factory – returns an AudioDecoderPoolImpl on Android,
 *        nullptr on platforms without a native implementation.
 */
std::shared_ptr<AudioDecoderPool> createPlatformAudioDecoderPool();

} // namespace timeline
} // namespace video
} // namespace sdk
