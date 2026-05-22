#include "../../include/timeline/TimelineExporter.h"
#include "../../include/timeline/AudioMixer.h"
#include "../../include/timeline/SyncClock.h"

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#define LOG_TAG "TimelineExporterAndroid"
#include "../../include/Log.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

// NDK constants missing in older headers or different names
#ifndef AMEDIACODEC_BUFFER_FLAG_KEY_FRAME
#define AMEDIACODEC_BUFFER_FLAG_KEY_FRAME 1
#endif

namespace sdk {
namespace video {
namespace timeline {

typedef media_status_t (*PFN_AMediaCodec_createInputSurface)(AMediaCodec*, ANativeWindow**);
typedef media_status_t (*PFN_AMediaCodec_signalEndOfInputStream)(AMediaCodec*);

class TimelineExporterAndroid : public TimelineExporter {
public:
    TimelineExporterAndroid()
        : m_width(0), m_height(0), m_fps(30), m_bitrate(0),
          m_state(State::IDLE), m_progress(0.0f), m_chunkDurationNs(0) {}

    sdk::video::timeline::SyncClock m_syncClock;

    void setAudioMixer(std::shared_ptr<AudioMixer> mixer) override {
        m_audioMixer = std::move(mixer);
    }

    ~TimelineExporterAndroid() override {
        cancel();
        if (m_exportThread.joinable()) {
            m_exportThread.join();
        }
    }

    Result configure(const std::string& outputPath, int width, int height, int fps, int bitrate) override {
        if (m_state != State::IDLE && m_state != State::COMPLETED && m_state != State::FAILED && m_state != State::CANCELED) {
            LOGE("Cannot configure while running, current state: %d", (int)m_state.load());
            return Result::error(ErrorCode::ERR_EXPORTER_ALREADY_RUNNING, "Cannot configure while running");
        }
        LOGI("Configuring exporter: path=%s, size=%dx%d, fps=%d, bitrate=%d", outputPath.c_str(), width, height, fps, bitrate);
        m_outputPath = outputPath;
        m_width = width;
        m_height = height;
        m_fps = fps;
        m_bitrate = bitrate;
        m_state = State::IDLE;
        m_progress = 0.0f;
        return Result::ok();
    }

    void configureChunking(int64_t chunkDurationNs, ChunkCallback onChunkReady) override {
        m_chunkDurationNs = chunkDurationNs;
        m_onChunkReady = onChunkReady;
    }

    Result exportAsync(std::shared_ptr<Timeline> timeline,
                     std::shared_ptr<Compositor> compositor,
                     ProgressCallback onProgress,
                     CompletionCallback onComplete) override {

        if (m_state == State::STARTING || m_state == State::EXPORTING) {
            LOGE("Export already in progress, current state: %d", (int)m_state.load());
            return Result::error(ErrorCode::ERR_EXPORTER_ALREADY_RUNNING, "Export already in progress");
        }

        if (m_outputPath.empty() || m_width <= 0 || m_height <= 0) {
            LOGE("Exporter not properly configured: path=%s, size=%dx%d", m_outputPath.c_str(), m_width, m_height);
            return Result::error(ErrorCode::ERR_EXPORTER_NOT_CONFIGURED, "Exporter not properly configured");
        }

        m_state = State::STARTING;
        m_progress = 0.0f;

        if (m_exportThread.joinable()) {
            m_exportThread.join();
        }

        m_exportThread = std::thread([this, timeline, compositor, onProgress, onComplete]() {
            auto start_time = std::chrono::high_resolution_clock::now();
            LOGI("Export thread started");
            Result res = runExport(timeline, compositor, onProgress);
            auto end_time = std::chrono::high_resolution_clock::now();
            float total_duration_s = std::chrono::duration<float>(end_time - start_time).count();

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                if (res.isOk()) {
                    m_state = State::COMPLETED;
                    LOGI("Export completed successfully in %.2f seconds", total_duration_s);
                } else if (res.getErrorCode() == ErrorCode::ERR_EXPORTER_CANCELLED) {
                    m_state = State::CANCELED;
                    LOGI("Export canceled after %.2f seconds", total_duration_s);
                } else {
                    m_state = State::FAILED;
                    LOGE("Export failed after %.2f seconds: %s", total_duration_s, res.getMessage().c_str());
                }
            }

            if (onComplete) {
                onComplete(res);
            }
        });

        return Result::ok();
    }

    void cancel() override {
        m_state = State::CANCELED;
    }

    State getState() const override {
        return m_state;
    }

    float getProgress() const override {
        return m_progress;
    }

private:
    Result runExport(std::shared_ptr<Timeline> timeline,
                     std::shared_ptr<Compositor> compositor,
                     ProgressCallback onProgress) {

        if (!timeline || !compositor) return Result::error(ErrorCode::ERR_TIMELINE_NULL, "Invalid timeline or compositor");

        m_state = State::EXPORTING;

        // Dynamic link to API 26+ functions
        PFN_AMediaCodec_createInputSurface p_createInputSurface = (PFN_AMediaCodec_createInputSurface)dlsym(RTLD_DEFAULT, "AMediaCodec_createInputSurface");
        PFN_AMediaCodec_signalEndOfInputStream p_signalEndOfInputStream = (PFN_AMediaCodec_signalEndOfInputStream)dlsym(RTLD_DEFAULT, "AMediaCodec_signalEndOfInputStream");

        if (!p_createInputSurface) {
            return Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "AMediaCodec_createInputSurface not available (API 26+ required)");
        }

        // --- 1. Resource Management Helpers (RAII-like) ---
        struct ResourceGuard {
            AMediaCodec* encoder      = nullptr;
            AMediaCodec* audioEncoder = nullptr;
            AMediaMuxer* muxer = nullptr;
            ANativeWindow* surface = nullptr;
            EGLDisplay display = EGL_NO_DISPLAY;
            EGLSurface eglSurface = EGL_NO_SURFACE;
            EGLContext eglContext = EGL_NO_CONTEXT;
            FILE* fp = nullptr;
            AMediaFormat* videoFormat = nullptr;
            AMediaFormat* audioFormat = nullptr;

            ~ResourceGuard() {
                if (eglContext != EGL_NO_CONTEXT) {
                    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                    eglDestroyContext(display, eglContext);
                }
                if (eglSurface != EGL_NO_SURFACE) eglDestroySurface(display, eglSurface);
                if (encoder) {
                    AMediaCodec_stop(encoder);
                    AMediaCodec_delete(encoder);
                }
                if (audioEncoder) {
                    AMediaCodec_stop(audioEncoder);
                    AMediaCodec_delete(audioEncoder);
                }
                if (muxer) {
                    AMediaMuxer_stop(muxer);
                    AMediaMuxer_delete(muxer);
                }
                if (surface) ANativeWindow_release(surface);
                if (fp) fclose(fp);
                if (videoFormat) AMediaFormat_delete(videoFormat);
                if (audioFormat) AMediaFormat_delete(audioFormat);
            }
        } g;

        // --- 2. Configure MediaCodec Encoder ---
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, m_width);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, m_height);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, m_bitrate > 0 ? m_bitrate : 4000000);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, m_fps);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 2130708361); // COLOR_FormatSurface

        g.encoder = AMediaCodec_createEncoderByType("video/avc");
        if (!g.encoder) {
            AMediaFormat_delete(format);
            return Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Failed to create encoder");
        }

        media_status_t status = AMediaCodec_configure(g.encoder, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        AMediaFormat_delete(format);
        if (status != AMEDIA_OK) return Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Failed to configure encoder");

        status = p_createInputSurface(g.encoder, &g.surface);
        if (status != AMEDIA_OK || !g.surface) return Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Failed to create input surface");

        status = AMediaCodec_start(g.encoder);
        if (status != AMEDIA_OK) return Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Failed to start encoder");

        // --- 2b. Configure AAC Audio Encoder (optional, non-fatal if unavailable) ---
        constexpr int AAC_SAMPLE_RATE    = AudioMixer::TARGET_SAMPLE_RATE;  // 44100
        constexpr int AAC_CHANNELS       = AudioMixer::TARGET_CHANNELS;     // 2
        constexpr int AAC_FRAME_SAMPLES  = 1024;   // standard AAC-LC frame size
        constexpr int AUDIO_BITRATE      = 128000;
        if (m_audioMixer) {
            AMediaFormat* af = AMediaFormat_new();
            AMediaFormat_setString(af, AMEDIAFORMAT_KEY_MIME,         "audio/mp4a-latm");
            AMediaFormat_setInt32 (af, AMEDIAFORMAT_KEY_SAMPLE_RATE,  AAC_SAMPLE_RATE);
            AMediaFormat_setInt32 (af, AMEDIAFORMAT_KEY_CHANNEL_COUNT, AAC_CHANNELS);
            AMediaFormat_setInt32 (af, AMEDIAFORMAT_KEY_BIT_RATE,     AUDIO_BITRATE);
            AMediaFormat_setInt32 (af, AMEDIAFORMAT_KEY_AAC_PROFILE,  2);  // AAC-LC
            g.audioEncoder = AMediaCodec_createEncoderByType("audio/mp4a-latm");
            if (!g.audioEncoder) {
                LOGE("AAC encoder unavailable — exporting video-only");
            } else {
                media_status_t ast = AMediaCodec_configure(g.audioEncoder, af, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
                if (ast != AMEDIA_OK) {
                    LOGE("AAC configure failed (%d) — exporting video-only", ast);
                    AMediaCodec_delete(g.audioEncoder);
                    g.audioEncoder = nullptr;
                } else {
                    AMediaCodec_start(g.audioEncoder);
                    LOGI("AAC encoder started (%d Hz, %d ch, %d bps)", AAC_SAMPLE_RATE, AAC_CHANNELS, AUDIO_BITRATE);
                }
            }
            AMediaFormat_delete(af);
        }

        // --- 3. Setup Headless EGL Context ---
        g.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(g.display, 0, 0);

        const EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_RECORDABLE_ANDROID, 1,
            EGL_NONE
        };

        EGLConfig config;
        EGLint numConfigs;
        eglChooseConfig(g.display, attribs, &config, 1, &numConfigs);

        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        g.eglContext = eglCreateContext(g.display, config, EGL_NO_CONTEXT, contextAttribs);
        if (g.eglContext == EGL_NO_CONTEXT) return Result::error(ErrorCode::ERR_EXPORTER_GL_CONTEXT_FAILED, "Failed to create EGL context");

        g.eglSurface = eglCreateWindowSurface(g.display, config, g.surface, nullptr);
        if (g.eglSurface == EGL_NO_SURFACE) return Result::error(ErrorCode::ERR_EXPORTER_GL_CONTEXT_FAILED, "Failed to create EGL surface");

        if (!eglMakeCurrent(g.display, g.eglSurface, g.eglSurface, g.eglContext)) {
            return Result::error(ErrorCode::ERR_EXPORTER_GL_CONTEXT_FAILED, "Failed to eglMakeCurrent");
        }

        typedef EGLBoolean (EGLAPIENTRYP PFNEGLPRESENTATIONTIMEANDROIDPROC)(EGLDisplay dpy, EGLSurface sur, khronos_stime_nanoseconds_t time);
        auto eglPresentationTimeANDROID = (PFNEGLPRESENTATIONTIMEANDROIDPROC)eglGetProcAddress("eglPresentationTimeANDROID");

        // --- 4. Setup Muxer ---
        std::string currentChunkPath = m_outputPath;
        int chunkIndex = 0;
        if (m_chunkDurationNs > 0) {
            currentChunkPath = m_outputPath + "_chunk_" + std::to_string(chunkIndex) + ".mp4";
        }

        g.fp = fopen(currentChunkPath.c_str(), "wb");
        if (!g.fp) return Result::error(ErrorCode::ERR_EXPORTER_IO_ERROR, "Failed to open output file: " + currentChunkPath);
        int fd = fileno(g.fp);

        g.muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        if (!g.muxer) return Result::error(ErrorCode::ERR_EXPORTER_MUXER_INIT_FAILED, "Failed to create muxer");

        ssize_t videoTrackIndex = -1;
        ssize_t audioTrackIndex = -1;
        bool muxerStarted      = false;
        bool videoFormatReady  = false;
        bool audioFormatReady  = !m_audioMixer || !g.audioEncoder;  // true when no audio path
        bool audioEncoderEOS   = false;
        int64_t audioPtsMicros = 0;
        std::vector<int16_t> audioPcmBuf;

        // --- 5. Render Loop ---
        int64_t totalDurationNs = timeline->getTotalDuration();
        if (totalDurationNs <= 0) return Result::error(ErrorCode::ERR_TIMELINE_NULL, "Timeline duration is zero");

        int64_t frameDurationNs = 1000000000 / m_fps;
        int64_t currentTimeNs = 0;
        int64_t lastChunkStartNs = 0;
        m_syncClock.reset();
        bool encoderEOS = false;
        bool inputEOS = false;

        FrameBufferPtr screenFb = std::make_shared<FrameBuffer>(m_width, m_height, 0);

        while (!encoderEOS) {
            if (m_state == State::CANCELED) {
                return Result::error(ErrorCode::ERR_EXPORTER_CANCELLED, "Export canceled by user");
            }

            // Feed input
            if (!inputEOS && currentTimeNs <= totalDurationNs) {
                glViewport(0, 0, m_width, m_height);
                glClearColor(0,0,0,1);
                glClear(GL_COLOR_BUFFER_BIT);

                Result renderRes = compositor->renderFrameAtTime(currentTimeNs, screenFb);
                if (!renderRes.isOk()) {
                    LOGE("Export render failed at %lld: %s", (long long)currentTimeNs, renderRes.getMessage().c_str());
                    return renderRes;
                }

                if (eglPresentationTimeANDROID) {
                    eglPresentationTimeANDROID(g.display, g.eglSurface, currentTimeNs);
                }
                eglSwapBuffers(g.display, g.eglSurface);

                // Feed audio PCM for this video frame's time window
                if (g.audioEncoder && m_audioMixer && !audioEncoderEOS) {
                    auto pcm = m_audioMixer->mixAudioAtTime(currentTimeNs, frameDurationNs);
                    audioPcmBuf.insert(audioPcmBuf.end(), pcm.begin(), pcm.end());
                    while ((int)audioPcmBuf.size() >= AAC_FRAME_SAMPLES * AAC_CHANNELS) {
                        ssize_t inIdx = AMediaCodec_dequeueInputBuffer(g.audioEncoder, 0);
                        if (inIdx < 0) break;
                        size_t inSz;
                        uint8_t* inBuf = AMediaCodec_getInputBuffer(g.audioEncoder, inIdx, &inSz);
                        size_t copyBytes = std::min(inSz, (size_t)(AAC_FRAME_SAMPLES * AAC_CHANNELS * sizeof(int16_t)));
                        memcpy(inBuf, audioPcmBuf.data(), copyBytes);
                        AMediaCodec_queueInputBuffer(g.audioEncoder, inIdx, 0, copyBytes, audioPtsMicros, 0);
                        audioPcmBuf.erase(audioPcmBuf.begin(), audioPcmBuf.begin() + AAC_FRAME_SAMPLES * AAC_CHANNELS);
                        audioPtsMicros += (int64_t)AAC_FRAME_SAMPLES * 1000000LL / AAC_SAMPLE_RATE;
                    }
                }

                m_progress = static_cast<float>(currentTimeNs) / static_cast<float>(totalDurationNs);
                if (onProgress) {
                    onProgress(m_progress);
                }

                currentTimeNs += frameDurationNs;

                if (currentTimeNs > totalDurationNs) {
                    if (p_signalEndOfInputStream) {
                        p_signalEndOfInputStream(g.encoder);
                    }
                    inputEOS = true;
                }
            }

            // Drain audio encoder (interleaved with video drain below)
            if (g.audioEncoder && !audioEncoderEOS) {
                AMediaCodecBufferInfo aInfo;
                ssize_t aIdx = AMediaCodec_dequeueOutputBuffer(g.audioEncoder, &aInfo, 0);
                if (aIdx >= 0) {
                    if (!(aInfo.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) &&
                        aInfo.size > 0 && muxerStarted && audioTrackIndex >= 0) {
                        size_t aSz;
                        uint8_t* aBuf = AMediaCodec_getOutputBuffer(g.audioEncoder, aIdx, &aSz);
                        AMediaMuxer_writeSampleData(g.muxer, audioTrackIndex, aBuf, &aInfo);
                        m_syncClock.updateAudioTs(aInfo.presentationTimeUs * 1000LL);
                    }
                    if (aInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) audioEncoderEOS = true;
                    AMediaCodec_releaseOutputBuffer(g.audioEncoder, aIdx, false);
                } else if (aIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED && !audioFormatReady) {
                    if (g.audioFormat) AMediaFormat_delete(g.audioFormat);
                    g.audioFormat   = AMediaCodec_getOutputFormat(g.audioEncoder);
                    audioTrackIndex = AMediaMuxer_addTrack(g.muxer, g.audioFormat);
                    audioFormatReady = true;
                    LOGI("Audio track added (index %zd)", audioTrackIndex);
                    if (videoFormatReady) {
                        AMediaMuxer_start(g.muxer);
                        muxerStarted = true;
                        LOGI("Muxer started (video+audio)");
                    }
                }
            }

            // Drain encoder
            AMediaCodecBufferInfo info;
            ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(g.encoder, &info, inputEOS ? 10000 : 0);
            if (outBufIdx >= 0) {
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                    AMediaCodec_releaseOutputBuffer(g.encoder, outBufIdx, false);
                    continue;
                }

                if (info.size > 0 && muxerStarted) {
                    size_t bufSize = 0;
                    uint8_t* buf = AMediaCodec_getOutputBuffer(g.encoder, outBufIdx, &bufSize);

                    // Chunking logic
                    if (m_chunkDurationNs > 0 && (info.flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME) != 0) {
                        int64_t ptsNs = info.presentationTimeUs * 1000LL;
                        if (ptsNs - lastChunkStartNs >= m_chunkDurationNs) {
                            AMediaMuxer_stop(g.muxer);
                            AMediaMuxer_delete(g.muxer);
                            fclose(g.fp);

                            if (m_onChunkReady) {
                                m_onChunkReady(currentChunkPath, chunkIndex);
                            }

                            chunkIndex++;
                            currentChunkPath = m_outputPath + "_chunk_" + std::to_string(chunkIndex) + ".mp4";
                            g.fp = fopen(currentChunkPath.c_str(), "wb");
                            if (!g.fp) return Result::error(ErrorCode::ERR_EXPORTER_IO_ERROR, "Failed to open chunk file");

                            fd = fileno(g.fp);
                            g.muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
                            videoTrackIndex = AMediaMuxer_addTrack(g.muxer, g.videoFormat);
                            AMediaMuxer_start(g.muxer);
                            lastChunkStartNs = ptsNs;
                        }
                    }

                    AMediaMuxer_writeSampleData(g.muxer, videoTrackIndex, buf, &info);
                    m_syncClock.updateVideoTs(info.presentationTimeUs * 1000LL);
                    if (!m_syncClock.isInSync()) {
                        LOGW("A/V sync drift detected: smoothed=%lld ms (threshold=%lld ms)",
                             (long long)(m_syncClock.getSmoothedOffsetNs() / 1'000'000LL),
                             (long long)(sdk::video::timeline::SyncClock::SYNC_THRESHOLD_NS / 1'000'000LL));
                    }
                }

                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                    encoderEOS = true;
                }

                AMediaCodec_releaseOutputBuffer(g.encoder, outBufIdx, false);
            } else if (outBufIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (g.videoFormat) AMediaFormat_delete(g.videoFormat);
                g.videoFormat   = AMediaCodec_getOutputFormat(g.encoder);
                videoTrackIndex = AMediaMuxer_addTrack(g.muxer, g.videoFormat);
                videoFormatReady = true;
                LOGI("Video track added (index %zd)", videoTrackIndex);
                if (audioFormatReady) {
                    AMediaMuxer_start(g.muxer);
                    muxerStarted = true;
                    LOGI("Muxer started (%s)", g.audioEncoder ? "video+audio" : "video-only");
                }
            }
        }

        // --- 6. Flush remaining audio and drain AAC encoder to EOS ---
        if (g.audioEncoder && !audioEncoderEOS) {
            // Submit any buffered PCM with EOS flag
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(g.audioEncoder, 50000);
            if (inIdx >= 0) {
                size_t inSz;
                uint8_t* inBuf = AMediaCodec_getInputBuffer(g.audioEncoder, inIdx, &inSz);
                size_t copyBytes = std::min(inSz, audioPcmBuf.size() * sizeof(int16_t));
                if (copyBytes > 0) memcpy(inBuf, audioPcmBuf.data(), copyBytes);
                AMediaCodec_queueInputBuffer(g.audioEncoder, inIdx, 0, copyBytes,
                                             audioPtsMicros, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            }
            while (!audioEncoderEOS) {
                AMediaCodecBufferInfo aInfo;
                ssize_t aIdx = AMediaCodec_dequeueOutputBuffer(g.audioEncoder, &aInfo, 50000);
                if (aIdx >= 0) {
                    if (!(aInfo.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) &&
                        aInfo.size > 0 && muxerStarted && audioTrackIndex >= 0) {
                        size_t aSz;
                        uint8_t* aBuf = AMediaCodec_getOutputBuffer(g.audioEncoder, aIdx, &aSz);
                        AMediaMuxer_writeSampleData(g.muxer, audioTrackIndex, aBuf, &aInfo);
                        m_syncClock.updateAudioTs(aInfo.presentationTimeUs * 1000LL);
                    }
                    if (aInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) audioEncoderEOS = true;
                    AMediaCodec_releaseOutputBuffer(g.audioEncoder, aIdx, false);
                } else if (aIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED && !audioFormatReady) {
                    if (g.audioFormat) AMediaFormat_delete(g.audioFormat);
                    g.audioFormat   = AMediaCodec_getOutputFormat(g.audioEncoder);
                    audioTrackIndex = AMediaMuxer_addTrack(g.muxer, g.audioFormat);
                    audioFormatReady = true;
                    if (videoFormatReady && !muxerStarted) {
                        AMediaMuxer_start(g.muxer);
                        muxerStarted = true;
                    }
                } else {
                    break;  // timeout — encoder may have already signaled EOS
                }
            }
            LOGI("Audio encoder drained, EOS=%d", (int)audioEncoderEOS);
        }

        if (m_chunkDurationNs > 0 && m_state != State::CANCELED && m_onChunkReady) {
            m_onChunkReady(currentChunkPath, chunkIndex);
        }

        m_progress = 1.0f;
        return Result::ok();
    }

    std::string m_outputPath;
    int m_width;
    int m_height;
    int m_fps;
    int m_bitrate;

    int64_t m_chunkDurationNs;
    ChunkCallback m_onChunkReady;

    std::shared_ptr<AudioMixer> m_audioMixer;

    std::atomic<State> m_state;
    std::mutex m_stateMutex;
    std::atomic<float> m_progress;
    std::thread m_exportThread;
};

std::unique_ptr<TimelineExporter> TimelineExporter::create() {
    return std::make_unique<TimelineExporterAndroid>();
}

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // __ANDROID__
