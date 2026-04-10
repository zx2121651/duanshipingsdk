#include "../../include/timeline/AudioMixer.h"
#include <algorithm>

namespace sdk {
namespace video {
namespace timeline {

AudioMixer::AudioMixer(std::shared_ptr<Timeline> timeline, AudioDecoderPoolPtr decoderPool)
    : m_timeline(timeline), m_decoderPool(decoderPool) {}

std::vector<int16_t> AudioMixer::mixAudioAtTime(int64_t timelineNs, int64_t durationNs) {
    if (!m_timeline || !m_decoderPool) return {};

    // Note: Assuming standard 44.1kHz, 2 channels (Stereo).
    // Samples needed = (durationNs / 1,000,000,000) * 44100 * 2 channels.
    const int SAMPLE_RATE = 44100;
    const int CHANNELS = 2;
    size_t samplesNeeded = static_cast<size_t>((durationNs / 1000000000.0) * SAMPLE_RATE * CHANNELS);

    // Safety check: Avoid huge allocations if duration is wrong
    if (samplesNeeded <= 0 || samplesNeeded > SAMPLE_RATE * CHANNELS * 5) {
        samplesNeeded = 4096;
    }

    // 1. Prepare a 32-bit integer accumulation buffer to prevent overflow during multi-track addition
    std::vector<int32_t> accumulationBuffer(samplesNeeded, 0);
    bool hasAudioData = false;

    // 2. Fetch active audio clips (assuming Timeline provides this. For now we use getActiveVideoClipsAtTime
    // and assume they contain audio, or getActiveAudioClipsAtTime if implemented).
    // For architectural simulation, we assume Video clips act as AV clips.
    m_timeline->getActiveAudioClipsAtTime(timelineNs, m_activeClips);

    // 3. Extract and Mix
    for (const auto& clip : m_activeClips) {
        // Calculate the relative time inside the media source
        // Example: If clip is placed at Timeline=5s, TrimIn=2s, and we want Timeline=6s -> localTime = 6-5+2 = 3s
        int64_t localTimeNs = (timelineNs - clip->getTimelineIn()) * clip->getSpeed() + clip->getTrimIn();

        // Duration also needs speed adjustment
        int64_t localDurationNs = static_cast<int64_t>(durationNs * clip->getSpeed());

        // Fetch decoded PCM chunk from the DecoderPool
        std::vector<int16_t> pcmData = m_decoderPool->getPcmData(clip->getId(), localTimeNs, localDurationNs);

        if (!pcmData.empty()) {
            hasAudioData = true;
            // Get dynamically interpolated volume from keyframes
            int64_t clipRelativeNs = timelineNs - clip->getTimelineIn();
            float volume = clip->getVolume(clipRelativeNs);

            // Limit loop to the smallest buffer size to prevent out-of-bounds
            size_t mixCount = std::min(accumulationBuffer.size(), pcmData.size());

            for (size_t i = 0; i < mixCount; ++i) {
                // Apply volume multiplier and add to 32-bit accumulator
                accumulationBuffer[i] += static_cast<int32_t>(pcmData[i] * volume);
            }
        }
    }

    std::vector<int16_t> finalOutput(samplesNeeded, 0);

    // 4. Clipping Protection (Hard Limiter)
    if (hasAudioData) {
        applyClippingProtection(accumulationBuffer, finalOutput);
    }

    return finalOutput;
}

void AudioMixer::applyClippingProtection(std::vector<int32_t>& mixBuffer, std::vector<int16_t>& outputBuffer) {
    // 基础防爆音：直接截断 (Hard Limiter)
    // 超过 16-bit 符号整型范围 [-32768, 32767] 的值会被强制钳制，防止整数溢出导致的尖锐杂音。

    // 后期中期演进：可以替换为 Dynamic Range Compressor (DRC) 或 Soft Clipper。

    for (size_t i = 0; i < mixBuffer.size(); ++i) {
        int32_t sample = mixBuffer[i];

        if (sample > 32767) {
            sample = 32767;
        } else if (sample < -32768) {
            sample = -32768;
        }

        outputBuffer[i] = static_cast<int16_t>(sample);
    }
}

} // namespace timeline
} // namespace video
} // namespace sdk
