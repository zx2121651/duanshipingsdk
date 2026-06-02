#pragma once
/**
 * VoiceChangerFilter.h
 *
 * 实时音频变声/音效处理器（音调变换 + 混响 + 变速）。
 *
 * 架构：
 *   - 纯 C++ DSP，不依赖外部库（平台可通过 setSoundTouchBackend 注入 SoundTouch）
 *   - 内置简单 pitch-shift（基于 OLA 算法，低延迟，质量适合实时预览）
 *   - 高质量模式：由平台层注入 SoundTouch / RubberBand 接口
 *
 * 预设（Preset）：
 *   ORIGINAL      原声
 *   CHIPMUNK      花栗鼠（高音调 +8 半音）
 *   GIANT         巨人（低音调 -8 半音）
 *   ROBOT         机器人（环形调制 + 混响）
 *   ECHO          回声
 *   REVERB        混响
 *   FAST          加速 1.5x（不变调）
 *   SLOW          减速 0.7x（不变调）
 *   CUSTOM        自定义参数
 *
 * 用法：
 *   VoiceChangerFilter vc;
 *   vc.setPreset(VoiceChangerFilter::Preset::CHIPMUNK);
 *   int16_t outBuf[1024];
 *   vc.process(inputPcm, 1024, outBuf, 44100, 1);
 */

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace sdk {
namespace video {
namespace audio {

class VoiceChangerFilter {
public:
    enum class Preset {
        ORIGINAL  = 0,
        CHIPMUNK  = 1,
        GIANT     = 2,
        ROBOT     = 3,
        ECHO      = 4,
        REVERB    = 5,
        FAST      = 6,
        SLOW      = 7,
        CUSTOM    = 8,
    };

    VoiceChangerFilter();
    ~VoiceChangerFilter();

    // ── 预设 ──────────────────────────────────────────────────────────────

    /** 应用预置效果，自动设置 pitchShift / tempo / reverb 参数。 */
    void setPreset(Preset preset);
    Preset getPreset() const { return m_preset; }

    /** 预设名称（调试 / UI 显示）。 */
    static const char* presetName(Preset p);

    // ── 自定义参数（CUSTOM 模式或覆盖预设） ──────────────────────────────

    /** 音调偏移（半音），[-12, +12]，0 = 不变调。 */
    void setPitchSemitones(float semitones) { m_pitchSemitones = semitones; }
    float getPitchSemitones() const         { return m_pitchSemitones; }

    /** 速度比率，[0.5, 2.0]，1.0 = 正常速度（独立于音调）。 */
    void setTempoRatio(float ratio) { m_tempoRatio = ratio; }
    float getTempoRatio() const     { return m_tempoRatio; }

    /** 混响湿度 [0,1]，0 = 纯干声，1 = 全混响。 */
    void setReverbWet(float wet) { m_reverbWet = wet; }
    float getReverbWet() const   { return m_reverbWet; }

    /** 回声参数：延迟（ms）和反馈量 [0,1]。 */
    void setEcho(float delayMs, float feedback) {
        m_echoDelayMs  = delayMs;
        m_echoFeedback = feedback;
    }

    /** 环形调制频率（Robot 效果用），Hz，0 = 关闭。 */
    void setRingModFreq(float hz) { m_ringModHz = hz; }

    // ── 处理 ──────────────────────────────────────────────────────────────

    /**
     * 处理一帧 PCM 音频。
     * @param input       输入 PCM，int16，交织格式（interleaved）
     * @param frameCount  采样帧数（= 总采样数 / 声道数）
     * @param output      输出缓冲区（调用方分配，长度 >= frameCount × channels）
     * @param sampleRate  采样率（Hz），如 44100 / 48000
     * @param channels    声道数（1 = 单声道，2 = 立体声）
     */
    void process(const int16_t* input, size_t frameCount,
                 int16_t* output, int sampleRate, int channels);

    /** 浮点版本（归一化 [-1.0, 1.0]）。 */
    void processFloat(const float* input, size_t frameCount,
                      float* output, int sampleRate, int channels);

    /** 重置所有内部状态（切换 preset 或 seek 后调用）。 */
    void reset();

    /**
     * 注入高质量 pitch-shift 后端（可选）。
     * 签名：(inputFloat, frameCount, pitchSemitones, tempoRatio, sampleRate, channels) → output float[]
     * 若未注入，使用内置 OLA 算法（低延迟但质量较低）。
     */
    using PitchShiftFn = std::function<void(
        const float*, size_t, float, float, int, int, float*)>;
    void setPitchShiftBackend(PitchShiftFn fn) { m_pitchShiftFn = std::move(fn); }

private:
    Preset m_preset          = Preset::ORIGINAL;
    float  m_pitchSemitones  = 0.f;
    float  m_tempoRatio      = 1.f;
    float  m_reverbWet       = 0.f;
    float  m_echoDelayMs     = 200.f;
    float  m_echoFeedback    = 0.4f;
    float  m_ringModHz       = 0.f;

    // Echo delay line
    std::vector<float> m_echoBuffer;
    size_t             m_echoWritePos = 0;
    int                m_lastSampleRate = 0;
    int                m_lastChannels   = 0;

    // Simple reverb state (Schroeder)
    static constexpr int kNumComb    = 4;
    static constexpr int kNumAllpass = 2;
    std::vector<float>   m_combBuf[kNumComb];
    int                  m_combPos[kNumComb]{};
    std::vector<float>   m_allpassBuf[kNumAllpass];
    int                  m_allpassPos[kNumAllpass]{};

    // Ring mod phase
    float m_ringPhase = 0.f;

    PitchShiftFn m_pitchShiftFn;

    // Internal scratch
    std::vector<float> m_floatIn;
    std::vector<float> m_floatOut;

    void ensureEchoBuffer(int sampleRate, int channels);
    void ensureReverbBuffers(int sampleRate);
    void applyPitch(const float* in, size_t frames, float* out,
                    int sr, int ch);
    void applyEcho (float* buf, size_t frames, int sr, int ch);
    void applyReverb(float* buf, size_t frames);
    void applyRingMod(float* buf, size_t frames, int sr, int ch);
};

} // namespace audio
} // namespace video
} // namespace sdk
