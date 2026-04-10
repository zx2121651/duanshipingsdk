#pragma once
#include "Timeline.h"
#include "AudioDecoder.h"
#include <vector>
#include <memory>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief NLE 音频混音引擎
 * 负责从 Timeline 的多轨音频（视频原声、音乐轨、配音轨）提取 PCM 数据，
 * 进行线性相加并执行裁剪保护（Hard Clipping Prevention），输出最终混音的 16-bit 缓冲区。
 */
class AudioMixer {
public:
    AudioMixer(std::shared_ptr<Timeline> timeline, AudioDecoderPoolPtr decoderPool);
    ~AudioMixer() = default;

    /**
     * @brief 获取多轨混合后的 PCM 音频
     * @param timelineNs 当前主时间线的起始请求时间
     * @param durationNs 请求时长 (通常根据需要的采样点数计算)
     * @return 混合后的 16-bit PCM 数组 (44.1kHz Stereo)
     */
    std::vector<int16_t> mixAudioAtTime(int64_t timelineNs, int64_t durationNs);

private:
    std::shared_ptr<Timeline> m_timeline;
    AudioDecoderPoolPtr m_decoderPool;

    // 基础的 Hard Clipping 保护算法：将 int32 累加值钳制在 int16 范围内
    void applyClippingProtection(std::vector<int32_t>& mixBuffer, std::vector<int16_t>& outputBuffer);
};

} // namespace timeline
} // namespace video
} // namespace sdk
