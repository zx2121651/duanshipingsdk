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
 * 混音器统一以 44100 Hz / Stereo 为目标采样率，输入不匹配时自动线性插值重采样。
 */
class AudioMixer {
public:
    static constexpr int TARGET_SAMPLE_RATE = 44100;
    static constexpr int TARGET_CHANNELS    = 2;

    AudioMixer(std::shared_ptr<Timeline> timeline, AudioDecoderPoolPtr decoderPool);
    ~AudioMixer() = default;

    /**
     * @brief 获取多轨混合后的 PCM 音频
     * @param timelineNs 当前主时间线的起始请求时间
     * @param durationNs 请求时长（纳秒）
     * @return 混合后的 16-bit PCM 数组（44100 Hz Stereo）
     */
    std::vector<int16_t> mixAudioAtTime(int64_t timelineNs, int64_t durationNs);

private:
    std::shared_ptr<Timeline> m_timeline;
    AudioDecoderPoolPtr m_decoderPool;
    std::vector<ClipPtr> m_activeClips;

    // 基础的 Hard Clipping 保护算法：将 int32 累加值钳制在 int16 范围内
    void applyClippingProtection(std::vector<int32_t>& mixBuffer, std::vector<int16_t>& outputBuffer);

    /**
     * @brief 线性插值重采样（无外部依赖）
     * 当 srcRate != TARGET_SAMPLE_RATE 时将输入缓冲转换为 44100 Hz。
     * @param input    原始 PCM（交错 Stereo, 16-bit）
     * @param srcRate  原始采样率（Hz）
     * @param dstRate  目标采样率，通常为 TARGET_SAMPLE_RATE
     * @param channels 声道数，通常为 TARGET_CHANNELS
     * @return 重采样后的 PCM（dstRate / Stereo）
     */
    static std::vector<int16_t> resample(const std::vector<int16_t>& input,
                                          int srcRate, int dstRate, int channels);
};

} // namespace timeline
} // namespace video
} // namespace sdk
