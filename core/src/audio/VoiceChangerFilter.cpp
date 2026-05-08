/**
 * VoiceChangerFilter.cpp
 *
 * 实时变声/音效 DSP 实现（无外部依赖）。
 *
 * Pitch shift: OLA (Overlap-Add) — 低延迟，适合实时预览。
 * 可通过 setPitchShiftBackend() 注入 SoundTouch / RubberBand。
 * Reverb: 4-comb + 2-allpass Schroeder 混响。
 * Echo: 单延迟线 + 反馈。
 * Ring Mod: 乘以正弦载波（机器人效果）。
 */

#include "../../include/audio/VoiceChangerFilter.h"

#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace sdk {
namespace video {
namespace audio {

// ---------------------------------------------------------------------------
// Preset definitions
// ---------------------------------------------------------------------------
const char* VoiceChangerFilter::presetName(Preset p) {
    switch (p) {
        case Preset::ORIGINAL: return "原声";
        case Preset::CHIPMUNK: return "花栗鼠";
        case Preset::GIANT:    return "巨人";
        case Preset::ROBOT:    return "机器人";
        case Preset::ECHO:     return "回声";
        case Preset::REVERB:   return "混响";
        case Preset::FAST:     return "加速";
        case Preset::SLOW:     return "慢放";
        case Preset::CUSTOM:   return "自定义";
        default:               return "未知";
    }
}

void VoiceChangerFilter::setPreset(Preset preset) {
    m_preset          = preset;
    m_pitchSemitones  = 0.f;
    m_tempoRatio      = 1.f;
    m_reverbWet       = 0.f;
    m_echoFeedback    = 0.f;
    m_ringModHz       = 0.f;

    switch (preset) {
        case Preset::CHIPMUNK:
            m_pitchSemitones = 8.f;
            break;
        case Preset::GIANT:
            m_pitchSemitones = -8.f;
            break;
        case Preset::ROBOT:
            m_ringModHz  = 80.f;
            m_reverbWet  = 0.25f;
            break;
        case Preset::ECHO:
            m_echoDelayMs  = 200.f;
            m_echoFeedback = 0.5f;
            break;
        case Preset::REVERB:
            m_reverbWet = 0.55f;
            break;
        case Preset::FAST:
            m_tempoRatio = 1.5f;
            break;
        case Preset::SLOW:
            m_tempoRatio = 0.7f;
            break;
        default:
            break;
    }
    reset();
}

// ---------------------------------------------------------------------------
VoiceChangerFilter::VoiceChangerFilter() = default;
VoiceChangerFilter::~VoiceChangerFilter() = default;

void VoiceChangerFilter::reset() {
    m_echoBuffer.clear(); m_echoWritePos = 0;
    m_lastSampleRate = 0; m_lastChannels = 0;
    for (int i = 0; i < kNumComb; ++i)    { m_combBuf[i].clear();    m_combPos[i] = 0; }
    for (int i = 0; i < kNumAllpass; ++i) { m_allpassBuf[i].clear(); m_allpassPos[i] = 0; }
    m_ringPhase = 0.f;
}

// ---------------------------------------------------------------------------
// ensureEchoBuffer
// ---------------------------------------------------------------------------
void VoiceChangerFilter::ensureEchoBuffer(int sampleRate, int channels) {
    if (sampleRate == m_lastSampleRate && channels == m_lastChannels) return;
    m_lastSampleRate = sampleRate;
    m_lastChannels   = channels;
    size_t delaySamples = static_cast<size_t>(m_echoDelayMs * 0.001f * sampleRate) * channels;
    delaySamples = std::max(delaySamples, (size_t)1);
    m_echoBuffer.assign(delaySamples, 0.f);
    m_echoWritePos = 0;
}

// ---------------------------------------------------------------------------
// ensureReverbBuffers (Schroeder — tuned comb delays)
// ---------------------------------------------------------------------------
static const int kCombDelays[4]    = {1557, 1617, 1491, 1422}; // samples @44100
static const int kAllpassDelays[2] = {225, 556};

void VoiceChangerFilter::ensureReverbBuffers(int sampleRate) {
    float ratio = (float)sampleRate / 44100.f;
    for (int i = 0; i < kNumComb; ++i) {
        int sz = std::max(1, (int)(kCombDelays[i] * ratio));
        if ((int)m_combBuf[i].size() != sz) {
            m_combBuf[i].assign(sz, 0.f);
            m_combPos[i] = 0;
        }
    }
    for (int i = 0; i < kNumAllpass; ++i) {
        int sz = std::max(1, (int)(kAllpassDelays[i] * ratio));
        if ((int)m_allpassBuf[i].size() != sz) {
            m_allpassBuf[i].assign(sz, 0.f);
            m_allpassPos[i] = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Pitch shift (OLA stub — resampling only, no time-stretch)
// ---------------------------------------------------------------------------
void VoiceChangerFilter::applyPitch(const float* in, size_t frames,
                                     float* out, int sr, int ch)
{
    if (m_pitchShiftFn) {
        m_pitchShiftFn(in, frames, m_pitchSemitones, m_tempoRatio, sr, ch, out);
        return;
    }

    if (std::fabs(m_pitchSemitones) < 0.01f && std::fabs(m_tempoRatio - 1.f) < 0.01f) {
        std::memcpy(out, in, frames * ch * sizeof(float));
        return;
    }

    // Simple resampling: pitch ratio = 2^(semitones/12)
    float pitchRatio = std::pow(2.f, m_pitchSemitones / 12.f);
    // Combined ratio: resample at (pitchRatio / tempoRatio) then adjust output length
    float readStep = pitchRatio;  // simplified OLA placeholder
    size_t totalSamples = frames * ch;

    for (size_t i = 0; i < frames; ++i) {
        float srcPos = i * readStep;
        size_t idx   = std::min((size_t)srcPos, frames - 1);
        size_t idx1  = std::min(idx + 1, frames - 1);
        float  frac  = srcPos - (float)idx;
        for (int c = 0; c < ch; ++c) {
            out[i * ch + c] = in[idx * ch + c] * (1.f - frac)
                            + in[idx1 * ch + c] * frac;
        }
    }
    (void)sr; (void)totalSamples;
}

// ---------------------------------------------------------------------------
// Echo
// ---------------------------------------------------------------------------
void VoiceChangerFilter::applyEcho(float* buf, size_t frames, int sr, int ch) {
    if (m_echoFeedback < 0.001f) return;
    ensureEchoBuffer(sr, ch);

    size_t bufLen = m_echoBuffer.size();
    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < ch; ++c) {
            float& delayed = m_echoBuffer[m_echoWritePos % bufLen];
            float  wet     = delayed * m_echoFeedback;
            float  dry     = buf[i * ch + c];
            delayed        = dry + wet;
            buf[i * ch + c] = dry + wet * 0.5f;
            ++m_echoWritePos;
        }
    }
}

// ---------------------------------------------------------------------------
// Reverb (Schroeder)
// ---------------------------------------------------------------------------
void VoiceChangerFilter::applyReverb(float* buf, size_t frames) {
    if (m_reverbWet < 0.001f) return;
    static const float kCombGain[4] = {0.805f, 0.827f, 0.783f, 0.764f};

    for (size_t i = 0; i < frames; ++i) {
        float in    = buf[i];
        float comb  = 0.f;

        // 4 parallel comb filters
        for (int k = 0; k < kNumComb; ++k) {
            auto& cb = m_combBuf[k];
            if (cb.empty()) continue;
            size_t sz = cb.size();
            float  out = cb[m_combPos[k]];
            comb += out;
            cb[m_combPos[k]] = in + out * kCombGain[k];
            m_combPos[k] = (m_combPos[k] + 1) % (int)sz;
        }
        comb *= 0.25f;

        // 2 series allpass filters
        for (int k = 0; k < kNumAllpass; ++k) {
            auto& ab = m_allpassBuf[k];
            if (ab.empty()) continue;
            size_t sz = ab.size();
            float ap  = ab[m_allpassPos[k]];
            float outp = ap - comb;
            ab[m_allpassPos[k]] = comb + ap * 0.5f;
            m_allpassPos[k] = (m_allpassPos[k] + 1) % (int)sz;
            comb = outp;
        }

        buf[i] = buf[i] * (1.f - m_reverbWet) + comb * m_reverbWet;
    }
}

// ---------------------------------------------------------------------------
// Ring modulation
// ---------------------------------------------------------------------------
void VoiceChangerFilter::applyRingMod(float* buf, size_t frames, int sr, int ch) {
    if (m_ringModHz < 0.1f) return;
    float phaseStep = static_cast<float>(2.0 * M_PI * m_ringModHz / sr);
    for (size_t i = 0; i < frames; ++i) {
        float mod = std::sin(m_ringPhase);
        m_ringPhase += phaseStep;
        if (m_ringPhase > static_cast<float>(2.0 * M_PI))
            m_ringPhase -= static_cast<float>(2.0 * M_PI);
        for (int c = 0; c < ch; ++c)
            buf[i * ch + c] *= mod;
    }
}

// ---------------------------------------------------------------------------
// processFloat
// ---------------------------------------------------------------------------
void VoiceChangerFilter::processFloat(const float* input, size_t frameCount,
                                       float* output, int sampleRate, int channels)
{
    size_t total = frameCount * channels;

    // 1. Pitch + tempo
    if (std::fabs(m_pitchSemitones) > 0.01f || std::fabs(m_tempoRatio - 1.f) > 0.01f) {
        applyPitch(input, frameCount, output, sampleRate, channels);
    } else {
        std::memcpy(output, input, total * sizeof(float));
    }

    // 2. Ring mod (mono mix for reverb)
    if (m_ringModHz > 0.1f) applyRingMod(output, frameCount, sampleRate, channels);

    // 3. Echo
    if (m_echoFeedback > 0.001f) applyEcho(output, frameCount, sampleRate, channels);

    // 4. Reverb (mono only for simplicity)
    if (m_reverbWet > 0.001f) {
        ensureReverbBuffers(sampleRate);
        if (channels == 1) {
            applyReverb(output, frameCount);
        } else {
            // Process each channel independently
            m_floatIn.resize(frameCount);
            for (int c = 0; c < channels; ++c) {
                for (size_t i = 0; i < frameCount; ++i)
                    m_floatIn[i] = output[i * channels + c];
                applyReverb(m_floatIn.data(), frameCount);
                for (size_t i = 0; i < frameCount; ++i)
                    output[i * channels + c] = m_floatIn[i];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// process (int16_t ↔ float conversion)
// ---------------------------------------------------------------------------
void VoiceChangerFilter::process(const int16_t* input, size_t frameCount,
                                  int16_t* output, int sampleRate, int channels)
{
    size_t total = frameCount * channels;
    m_floatIn.resize(total);
    m_floatOut.resize(total);

    for (size_t i = 0; i < total; ++i)
        m_floatIn[i] = input[i] / 32768.f;

    processFloat(m_floatIn.data(), frameCount, m_floatOut.data(), sampleRate, channels);

    for (size_t i = 0; i < total; ++i) {
        float v    = m_floatOut[i] * 32768.f;
        v          = std::max(-32768.f, std::min(32767.f, v));
        output[i]  = static_cast<int16_t>(v);
    }
}

} // namespace audio
} // namespace video
} // namespace sdk
