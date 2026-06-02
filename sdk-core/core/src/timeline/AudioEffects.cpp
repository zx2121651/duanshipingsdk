#include "../../include/timeline/AudioEffects.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// WSOLA 参数
//   WINDOW  : 分析/合成帧长度（samples）
//   SYNTH   : 合成跳步 = WINDOW / 2 (50% 重叠)
//   SEARCH  : 波形相似度搜索半径 (samples)
// ---------------------------------------------------------------------------
static constexpr int WSOLA_WINDOW = 256;
static constexpr int WSOLA_SYNTH  = WSOLA_WINDOW / 2;
static constexpr int WSOLA_SEARCH = 24;

// Hanning 窗系数 (归一化到 [0, 1])
static inline float hanning(int i, int n) {
    return 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (n - 1)));
}

// ---------------------------------------------------------------------------
// findBestOffset
// 在 analysisPos ± searchRange 范围内，通过归一化互相关寻找
// 与 output 当前末尾最吻合的 input 位置偏移量，以减少拼接噪音。
// ---------------------------------------------------------------------------
static int findBestOffset(
    const std::vector<float>& output, int outWritePos,
    const std::vector<float>& input,  int inputNominalPos,
    int halfWindow, int searchRange)
{
    int bestOffset = 0;
    float bestCorr = -1.0e9f;

    for (int off = -searchRange; off <= searchRange; ++off) {
        int ap = inputNominalPos + off;
        float corr = 0.0f, na = 0.0f, nb = 0.0f;

        for (int i = 0; i < halfWindow; ++i) {
            int oi = outWritePos - halfWindow + i;
            int ii = ap + i;

            float a = (oi >= 0 && oi < (int)output.size()) ? output[oi] : 0.0f;
            float b = (ii >= 0 && ii < (int)input.size())  ? input[ii]  : 0.0f;

            corr += a * b;
            na   += a * a;
            nb   += b * b;
        }

        float norm = std::sqrt(na * nb);
        float nc   = (norm > 1.0e-6f) ? corr / norm : 0.0f;
        if (nc > bestCorr) { bestCorr = nc; bestOffset = off; }
    }
    return bestOffset;
}

// ---------------------------------------------------------------------------
// wsolaChannel
// 对单声道 float PCM 做 WSOLA，输出长度约等于 input.size() / pitchRatio，
// 之后需要再线性重采样回原始长度。
// ---------------------------------------------------------------------------
static std::vector<float> wsolaChannel(const std::vector<float>& input, float pitchRatio) {
    const int WIN    = WSOLA_WINDOW;
    const int SYNTH  = WSOLA_SYNTH;
    const float ANA  = SYNTH * pitchRatio; // analysis hop (浮点，累积)

    int inputLen  = (int)input.size();
    // 输出帧数 ≈ input_len / pitchRatio（音调升高则压缩）
    int outLen    = static_cast<int>(inputLen / pitchRatio) + WIN * 2;
    std::vector<float> out(outLen, 0.0f);
    std::vector<float> env(outLen, 0.0f); // overlap-add normalization envelope

    float anaPos  = 0.0f;
    int   synPos  = 0;

    while (synPos + WIN <= outLen) {
        int ap = static_cast<int>(anaPos);
        ap = std::max(0, std::min(ap, inputLen - WIN));

        // 寻找最佳对齐偏移（仅在有足够历史输出时才做）
        if (synPos > WIN) {
            int off = findBestOffset(out, synPos, input, ap, WIN / 2, WSOLA_SEARCH);
            ap = std::max(0, std::min(ap + off, inputLen - WIN));
        }

        // Hanning 窗 overlap-add
        for (int i = 0; i < WIN; ++i) {
            float w = hanning(i, WIN);
            float s = input[ap + i];
            out[synPos + i] += w * s;
            env[synPos + i] += w * w;
        }

        synPos += SYNTH;
        anaPos += ANA;

        if (ap + (int)ANA >= inputLen) break;
    }

    // 归一化（消除窗叠加引入的幅度波动）
    for (int i = 0; i < outLen; ++i) {
        if (env[i] > 1.0e-6f) out[i] /= env[i];
    }
    // 截断到实际有意义的长度
    out.resize(std::min(outLen, static_cast<int>(inputLen / pitchRatio) + WIN));
    return out;
}

// ---------------------------------------------------------------------------
// linearResampleMono
// 线性插值把 src 重采样到目标帧数
// ---------------------------------------------------------------------------
static std::vector<float> linearResampleMono(const std::vector<float>& src, size_t dstLen) {
    size_t srcLen = src.size();
    if (srcLen == 0 || dstLen == 0) return {};
    std::vector<float> dst(dstLen);
    for (size_t i = 0; i < dstLen; ++i) {
        double pos  = (double)i * (srcLen - 1) / (dstLen - 1);
        size_t idx0 = std::min((size_t)pos, srcLen - 1);
        size_t idx1 = std::min(idx0 + 1, srcLen - 1);
        float  frac = (float)(pos - idx0);
        dst[i] = src[idx0] + frac * (src[idx1] - src[idx0]);
    }
    return dst;
}

// ---------------------------------------------------------------------------
// AudioEffects::pitchShift (公开 API)
// ---------------------------------------------------------------------------
std::vector<int16_t> AudioEffects::pitchShift(
    const std::vector<int16_t>& input,
    float pitchSemitones,
    int   channels,
    int   /*sampleRate*/)
{
    if (input.empty() || std::abs(pitchSemitones) < 0.01f) return input;

    // 钳制范围，防止极端值导致异常
    float clamped    = std::max(-24.0f, std::min(24.0f, pitchSemitones));
    float pitchRatio = std::pow(2.0f, clamped / 12.0f);  // e.g., +12 st → 2.0, -12 st → 0.5

    size_t numFrames = input.size() / (size_t)channels;
    std::vector<int16_t> output(input.size(), 0);

    for (int ch = 0; ch < channels; ++ch) {
        // 1. 提取单声道 float
        std::vector<float> mono(numFrames);
        for (size_t f = 0; f < numFrames; ++f) {
            mono[f] = static_cast<float>(input[f * (size_t)channels + (size_t)ch]) / 32768.0f;
        }

        // 2. WSOLA：输出长度 ≈ numFrames / pitchRatio
        std::vector<float> shifted = wsolaChannel(mono, pitchRatio);

        // 3. 重采样回原始帧数（保持时长不变）
        std::vector<float> resampled = linearResampleMono(shifted, numFrames);

        // 4. 写回交错输出
        for (size_t f = 0; f < numFrames; ++f) {
            float s = resampled[f];
            output[f * (size_t)channels + (size_t)ch] =
                static_cast<int16_t>(std::max(-32768.0f, std::min(32767.0f, s * 32768.0f)));
        }
    }

    return output;
}

// ---------------------------------------------------------------------------
// AudioEffects::noiseReduction — Wiener Filter 软降噪
//
// 算法（短时 Wiener 增益）：
//   1. 将 PCM 分成 FRAME_SIZE 个样本的短帧
//   2. 前 NOISE_FRAMES 帧用于估计底噪能量 (EPS + 滑动平均稳定)
//   3. 对每帧计算信噪比 SNR = frame_power / noise_floor
//   4. Wiener 增益 G = SNR / (SNR + lambda)，lambda = 1/strength
//   5. 对增益做时间平滑（一阶 IIR）防止突变杂音
//   6. 乘以增益写出
// ---------------------------------------------------------------------------
std::vector<int16_t> AudioEffects::noiseReduction(
    const std::vector<int16_t>& input,
    float strength,
    int   channels,
    int   /*sampleRate*/)
{
    if (input.empty() || strength < 0.01f) return input;

    strength = std::max(0.0f, std::min(1.0f, strength));
    // lambda: smaller = more aggressive suppression when strength→1
    const float lambda      = 1.0f / (strength + 1e-6f);
    const float SMOOTH_COEF = 0.7f;   // IIR gain smoothing coefficient

    // Frame size: 128 samples per channel group (2.9 ms @ 44100)
    const int FRAME_SIZE   = 128;
    const int NOISE_FRAMES = 8;        // use first ~23 ms to estimate noise floor

    size_t numFrames = input.size() / (size_t)channels;
    size_t numBlocks = numFrames / FRAME_SIZE;

    if (numBlocks == 0) return input;

    // ----------------------------------------------------------------
    // Pass 1: estimate per-channel noise floor from first NOISE_FRAMES blocks
    // ----------------------------------------------------------------
    std::vector<float> noiseFloor(channels, 1.0f); // power floor per channel

    int estBlocks = std::min((int)numBlocks, NOISE_FRAMES);
    for (int ch = 0; ch < channels; ++ch) {
        float powerSum = 0.0f;
        for (int b = 0; b < estBlocks; ++b) {
            for (int i = 0; i < FRAME_SIZE; ++i) {
                size_t idx = ((size_t)b * FRAME_SIZE + (size_t)i) * (size_t)channels + (size_t)ch;
                float s = static_cast<float>(input[idx]) / 32768.0f;
                powerSum += s * s;
            }
        }
        float avgPower = powerSum / (estBlocks * FRAME_SIZE);
        // Add small epsilon to prevent divide-by-zero on true silence
        noiseFloor[ch] = std::max(avgPower, 1e-8f);
    }

    // ----------------------------------------------------------------
    // Pass 2: apply block-wise Wiener gain
    // ----------------------------------------------------------------
    std::vector<int16_t> output(input);  // start as copy, apply gains in-place

    for (int ch = 0; ch < channels; ++ch) {
        float smoothGain = 1.0f;

        for (size_t b = 0; b < numBlocks; ++b) {
            // Compute block power
            float power = 0.0f;
            for (int i = 0; i < FRAME_SIZE; ++i) {
                size_t idx = (b * FRAME_SIZE + (size_t)i) * (size_t)channels + (size_t)ch;
                float s = static_cast<float>(input[idx]) / 32768.0f;
                power += s * s;
            }
            float avgPower = power / FRAME_SIZE;

            // Wiener gain: G = SNR / (SNR + lambda)
            float snr       = avgPower / noiseFloor[ch];
            float targetGain = snr / (snr + lambda);
            targetGain = std::max(0.0f, std::min(1.0f, targetGain));

            // Smooth gain transitions (prevent clicks)
            smoothGain = SMOOTH_COEF * smoothGain + (1.0f - SMOOTH_COEF) * targetGain;

            // Apply gain to block
            for (int i = 0; i < FRAME_SIZE; ++i) {
                size_t idx = (b * FRAME_SIZE + (size_t)i) * (size_t)channels + (size_t)ch;
                float s = static_cast<float>(input[idx]) * smoothGain;
                output[idx] = static_cast<int16_t>(
                    std::max(-32768.0f, std::min(32767.0f, s)));
            }
        }
    }

    return output;
}

// ---------------------------------------------------------------------------
// extractWaveformPeaks — PCM 波形峰值提取
//
// 算法：
//   1. 按 numBars 将帧序列等分为 numBars 个窗口
//   2. 每个窗口内对所有声道取绝对值最大值（峰值）
//   3. 全局最大峰值归一化至 [0,1]
// ---------------------------------------------------------------------------
std::vector<float> AudioEffects::extractWaveformPeaks(const std::vector<int16_t>& pcm,
                                                       int numBars,
                                                       int channels)
{
    std::vector<float> peaks(numBars, 0.0f);
    if (pcm.empty() || numBars <= 0 || channels <= 0) return peaks;

    const int totalFrames = static_cast<int>(pcm.size()) / channels;
    if (totalFrames == 0) return peaks;

    const float framesPerBar = static_cast<float>(totalFrames) / static_cast<float>(numBars);
    float globalMax = 1.0f; // 防止除零，至少为 1

    for (int bar = 0; bar < numBars; ++bar) {
        int startFrame = static_cast<int>(bar       * framesPerBar);
        int endFrame   = static_cast<int>((bar + 1) * framesPerBar);
        endFrame = std::min(endFrame, totalFrames);

        float barPeak = 0.0f;
        for (int frame = startFrame; frame < endFrame; ++frame) {
            for (int ch = 0; ch < channels; ++ch) {
                float sample = std::abs(static_cast<float>(pcm[frame * channels + ch]));
                if (sample > barPeak) barPeak = sample;
            }
        }
        peaks[bar] = barPeak;
        if (barPeak > globalMax) globalMax = barPeak;
    }

    // 归一化
    for (float& p : peaks) p /= globalMax;

    return peaks;
}

} // namespace timeline
} // namespace video
} // namespace sdk
