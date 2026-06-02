#pragma once
// AudioEffects.h — 音频特效处理工具集（无外部依赖）
//
// 当前实现的特效：
//   pitchShift: 基于 WSOLA (Waveform Similarity Overlap-Add) 的音调变换
//               - 保持时长不变，仅改变音调
//               - 支持 ±24 semitones 范围
//               - 无 FFT 依赖，纯时域处理

#include <vector>
#include <cstdint>

namespace sdk {
namespace video {
namespace timeline {

class AudioEffects {
public:
    /**
     * @brief WSOLA 音调变换（变声）
     *
     * 以 semitones 为单位移动音调，时长保持不变。
     * 正值升调（尖锐），负值降调（低沉）。
     *
     * @param input          原始 PCM（交错多声道 16-bit）
     * @param pitchSemitones 音调偏移量（半音），范围 [-24, +24]
     * @param channels       声道数（通常 2）
     * @param sampleRate     采样率（通常 44100）
     * @return               变调后的 PCM，与 input 等长
     */
    static std::vector<int16_t> pitchShift(const std::vector<int16_t>& input,
                                            float pitchSemitones,
                                            int   channels,
                                            int   sampleRate);

    /**
     * @brief Wiener Filter 软降噪
     *
     * 基于短时能量估计的 Wiener 增益：
     *   G = SNR / (SNR + 1/strength)
     * 前 `noiseEstimFrames` 帧作为噪声底噪估计（假设为静音段）。
     *
     * @param input              原始 PCM（交错多声道 16-bit）
     * @param strength           降噪强度 [0, 1]，0=关闭，1=最大抑制
     * @param channels           声道数
     * @param sampleRate         采样率（Hz）
     * @return                   降噪后的 PCM，与 input 等长
     */
    static std::vector<int16_t> noiseReduction(const std::vector<int16_t>& input,
                                                float strength,
                                                int   channels,
                                                int   sampleRate);

    /**
     * PCM 波形峰值提取 — 供剪辑编辑器波形 UI 使用。
     *
     * 将 PCM 数据按 numBars 个等宽窗口划分，每窗口取绝对值峰值，
     * 归一化到 [0.0, 1.0] 返回。多声道数据取声道最大值。
     *
     * @param pcm       16-bit 有符号 PCM 数据
     * @param numBars   目标波形柱数（通常等于 UI 宽度像素数）
     * @param channels  声道数（1=mono，2=stereo）
     * @return          长度为 numBars 的浮点数组，值域 [0, 1]
     */
    static std::vector<float> extractWaveformPeaks(const std::vector<int16_t>& pcm,
                                                   int numBars,
                                                   int channels);
};

} // namespace timeline
} // namespace video
} // namespace sdk
