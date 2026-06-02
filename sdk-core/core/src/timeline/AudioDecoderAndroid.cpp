#include "../../include/timeline/AudioDecoder.h"

#ifdef __ANDROID__
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#define LOG_TAG "AudioDecoderAndroid"
#include "../../include/Log.h"
#include <cstring>
#include <algorithm>
#include <map>
#include <mutex>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// AudioDecoderAndroid: decodes audio from a media file to 16-bit PCM via
// AMediaExtractor + AMediaCodec (NDK Media API).
// Outputs raw interleaved 16-bit PCM at the source sample rate.
// AudioMixer will resample to 44100 Hz if needed.
// ---------------------------------------------------------------------------
class AudioDecoderAndroid : public AudioDecoder {
public:
    AudioDecoderAndroid() = default;

    ~AudioDecoderAndroid() override {
        close();
    }

    Result open(const std::string& filePath) override {
        m_extractor = AMediaExtractor_new();
        if (!m_extractor) {
            return Result::error(ErrorCode::ERR_AUDIO_EXTRACTOR_CREATE_FAILED, "Failed to create AMediaExtractor for " + filePath);
        }

        media_status_t err = AMediaExtractor_setDataSource(m_extractor, filePath.c_str());
        if (err != AMEDIA_OK) {
            return Result::error(ErrorCode::ERR_AUDIO_SOURCE_OPEN_FAILED, "Failed to set audio data source: " + filePath);
        }

        int numTracks = (int)AMediaExtractor_getTrackCount(m_extractor);
        AMediaFormat* format = nullptr;
        const char* mime = nullptr;
        int audioTrackIdx = -1;

        for (int i = 0; i < numTracks; i++) {
            AMediaFormat* fmt = AMediaExtractor_getTrackFormat(m_extractor, i);
            const char* m = nullptr;
            AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m);
            if (m && strncmp(m, "audio/", 6) == 0) {
                audioTrackIdx = i;
                format = fmt;
                mime = m;
                break;
            }
            AMediaFormat_delete(fmt);
        }

        if (!format || audioTrackIdx < 0) {
            return Result::error(ErrorCode::ERR_AUDIO_TRACK_NOT_FOUND, "No audio track found in " + filePath);
        }

        AMediaExtractor_selectTrack(m_extractor, audioTrackIdx);

        // Read source sample rate and channel count
        int32_t sampleRate = 44100;
        int32_t channelCount = 2;
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channelCount);
        m_sourceSampleRate = sampleRate;
        m_channels = channelCount;

        m_codec = AMediaCodec_createDecoderByType(mime);
        AMediaFormat_delete(format);

        if (!m_codec) {
            return Result::error(ErrorCode::ERR_AUDIO_CODEC_CREATE_FAILED, "Failed to create audio codec for " + filePath);
        }

        // Re-fetch format for configure (using track format again)
        AMediaFormat* cfgFmt = AMediaExtractor_getTrackFormat(m_extractor, audioTrackIdx);
        err = AMediaCodec_configure(m_codec, cfgFmt, nullptr, nullptr, 0);
        AMediaFormat_delete(cfgFmt);

        if (err != AMEDIA_OK) {
            return Result::error(ErrorCode::ERR_AUDIO_CODEC_CONFIG_FAILED, "Failed to configure audio codec for " + filePath);
        }

        err = AMediaCodec_start(m_codec);
        if (err != AMEDIA_OK) {
            return Result::error(ErrorCode::ERR_AUDIO_CODEC_START_FAILED, "Failed to start audio codec for " + filePath);
        }

        m_filePath = filePath;
        m_isOpen = true;
        LOGI("AudioDecoderAndroid opened: %s, sampleRate=%d, channels=%d",
             filePath.c_str(), m_sourceSampleRate, m_channels);
        return Result::ok();
    }

    std::vector<int16_t> getPcmData(int64_t timeNs, int64_t durationNs) override {
        if (!m_isOpen) return {};

        // Seek to the requested position
        media_status_t seekErr = AMediaExtractor_seekTo(
            m_extractor, timeNs / 1000, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
        if (seekErr != AMEDIA_OK) {
            LOGE("AudioDecoderAndroid: seek failed to %lld us", (long long)(timeNs / 1000));
            return {};
        }
        AMediaCodec_flush(m_codec);

        int64_t endNs = timeNs + durationNs;
        std::vector<int16_t> output;
        output.reserve(static_cast<size_t>((durationNs / 1000000000.0) * m_sourceSampleRate * m_channels * 1.1));

        bool inputDone = false;
        bool outputDone = false;
        int maxIterations = 2000; // safety cap

        while (!outputDone && maxIterations-- > 0) {
            // Feed input
            if (!inputDone) {
                ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(m_codec, 5000);
                if (bufIdx >= 0) {
                    size_t bufSize = 0;
                    uint8_t* buf = AMediaCodec_getInputBuffer(m_codec, (size_t)bufIdx, &bufSize);
                    ssize_t sampleSize = AMediaExtractor_readSampleData(m_extractor, buf, bufSize);

                    if (sampleSize < 0) {
                        sampleSize = 0;
                        inputDone = true;
                    }

                    int64_t pts = AMediaExtractor_getSampleTime(m_extractor);
                    AMediaCodec_queueInputBuffer(m_codec, (size_t)bufIdx, 0, (size_t)sampleSize, pts,
                                                 inputDone ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);
                    if (!inputDone) AMediaExtractor_advance(m_extractor);
                }
            }

            // Drain output
            AMediaCodecBufferInfo info;
            ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(m_codec, &info, 5000);

            if (outIdx >= 0) {
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                    outputDone = true;
                }

                if (info.size > 0) {
                    int64_t ptsNs = info.presentationTimeUs * 1000;

                    // Only collect samples within the requested time window
                    if (ptsNs >= timeNs - 10000000LL && ptsNs < endNs) {
                        size_t outBufSize = 0;
                        uint8_t* outBuf = AMediaCodec_getOutputBuffer(m_codec, (size_t)outIdx, &outBufSize);
                        size_t sampleCount = info.size / sizeof(int16_t);
                        const int16_t* pcm = reinterpret_cast<const int16_t*>(outBuf);
                        output.insert(output.end(), pcm, pcm + sampleCount);
                    } else if (ptsNs >= endNs) {
                        // Passed the end of requested window
                        AMediaCodec_releaseOutputBuffer(m_codec, (size_t)outIdx, false);
                        outputDone = true;
                        break;
                    }
                }
                AMediaCodec_releaseOutputBuffer(m_codec, (size_t)outIdx, false);
            }
        }

        return output;
    }

    int getSourceSampleRate() const override {
        return m_sourceSampleRate;
    }

    void close() override {
        m_isOpen = false;
        if (m_codec) {
            AMediaCodec_stop(m_codec);
            AMediaCodec_delete(m_codec);
            m_codec = nullptr;
        }
        if (m_extractor) {
            AMediaExtractor_delete(m_extractor);
            m_extractor = nullptr;
        }
    }

private:
    AMediaExtractor* m_extractor = nullptr;
    AMediaCodec*     m_codec     = nullptr;
    std::string      m_filePath;
    int              m_sourceSampleRate = 44100;
    int              m_channels = 2;
    bool             m_isOpen   = false;
};

// ---------------------------------------------------------------------------
// AudioDecoderPoolImpl: concrete AudioDecoderPool for Android.
// Manages one AudioDecoderAndroid per registered clip.
// ---------------------------------------------------------------------------
class AudioDecoderPoolImpl : public AudioDecoderPool {
public:
    AudioDecoderPoolImpl() = default;
    ~AudioDecoderPoolImpl() override { clear(); }

    void registerMedia(const std::string& clipId, const std::string& sourcePath) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Close existing decoder if re-registering
        auto it = m_decoders.find(clipId);
        if (it != m_decoders.end() && it->second) {
            it->second->close();
        }
        auto decoder = std::make_shared<AudioDecoderAndroid>();
        auto res = decoder->open(sourcePath);
        if (!res.isOk()) {
            LOGE("AudioDecoderPoolImpl: failed to open %s: %s",
                 clipId.c_str(), res.getMessage().c_str());
            m_decoders[clipId] = nullptr; // mark as failed
        } else {
            m_decoders[clipId] = decoder;
        }
        m_paths[clipId] = sourcePath;
    }

    void releaseMedia(const std::string& clipId) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_decoders.find(clipId);
        if (it != m_decoders.end()) {
            if (it->second) it->second->close();
            m_decoders.erase(it);
        }
        m_paths.erase(clipId);
    }

    std::vector<int16_t> getPcmData(const std::string& clipId,
                                     int64_t localTimeNs,
                                     int64_t durationNs) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_decoders.find(clipId);
        if (it == m_decoders.end() || !it->second) return {};
        return it->second->getPcmData(localTimeNs, durationNs);
    }

    int getSourceSampleRate(const std::string& clipId) const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_decoders.find(clipId);
        if (it == m_decoders.end() || !it->second) return 44100;
        return it->second->getSourceSampleRate();
    }

private:
    void clear() {
        for (auto& p : m_decoders) {
            if (p.second) p.second->close();
        }
        m_decoders.clear();
        m_paths.clear();
    }

    mutable std::mutex m_mutex;
    std::map<std::string, std::shared_ptr<AudioDecoderAndroid>> m_decoders;
    std::map<std::string, std::string> m_paths;
};

// Factory
std::shared_ptr<AudioDecoderPool> createPlatformAudioDecoderPool() {
    return std::make_shared<AudioDecoderPoolImpl>();
}

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // __ANDROID__

// Non-Android stub factory so the header remains happy on other platforms
#ifndef __ANDROID__
namespace sdk { namespace video { namespace timeline {
std::shared_ptr<AudioDecoderPool> createPlatformAudioDecoderPool() {
    return nullptr;
}
}}}
#endif
