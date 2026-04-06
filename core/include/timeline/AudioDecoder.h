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
     * @param timeUs 请求的起始相对时间 (微秒)
     * @param durationUs 请求的持续时间 (微秒)
     * @return 16-bit PCM 样本数组 (交错双声道: L R L R...)
     */
    virtual std::vector<int16_t> getPcmData(int64_t timeUs, int64_t durationUs) = 0;

    virtual void close() = 0;
};

class AudioDecoderPool {
public:
    virtual ~AudioDecoderPool() = default;

    virtual void registerMedia(const std::string& clipId, const std::string& sourcePath) = 0;
    virtual void releaseMedia(const std::string& clipId) = 0;

    virtual std::vector<int16_t> getPcmData(const std::string& clipId, int64_t localTimeUs, int64_t durationUs) = 0;
};

using AudioDecoderPoolPtr = std::shared_ptr<AudioDecoderPool>;

} // namespace timeline
} // namespace video
} // namespace sdk
