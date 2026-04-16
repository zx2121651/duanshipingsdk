#include "../../include/timeline/TimelineExporter.h"

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <android/log.h>
#include <thread>
#include <atomic>
#include <mutex>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TimelineExporterAndroid", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TimelineExporterAndroid", __VA_ARGS__)

namespace sdk {
namespace video {
namespace timeline {

class TimelineExporterAndroid : public TimelineExporter {
public:
    TimelineExporterAndroid()
        : m_width(0), m_height(0), m_fps(30), m_bitrate(0),
          m_state(State::IDLE), m_progress(0.0f), m_chunkDurationNs(0) {}

    ~TimelineExporterAndroid() override {
        cancel();
        if (m_exportThread.joinable()) {
            m_exportThread.join();
        }
    }

    Result configure(const std::string& outputPath, int width, int height, int fps, int bitrate) override {
        if (m_state != State::IDLE && m_state != State::COMPLETED && m_state != State::FAILED && m_state != State::CANCELED) {
            return Result::error(ErrorCode::ERR_EXPORTER_ALREADY_RUNNING, "Cannot configure while running");
        }
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
            return Result::error(ErrorCode::ERR_EXPORTER_ALREADY_RUNNING, "Export already in progress");
        }

        if (m_outputPath.empty() || m_width <= 0 || m_height <= 0) {
            return Result::error(ErrorCode::ERR_EXPORTER_NOT_CONFIGURED, "Exporter not properly configured");
        }

        m_state = State::STARTING;
        m_progress = 0.0f;

        if (m_exportThread.joinable()) {
            m_exportThread.join();
        }

        m_exportThread = std::thread([this, timeline, compositor, onProgress, onComplete]() {
            Result res = runExport(timeline, compositor, onProgress);

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                if (res.isOk()) {
                    m_state = State::COMPLETED;
                } else if (res.getErrorCode() == ErrorCode::ERR_EXPORTER_CANCELLED) {
                    m_state = State::CANCELED;
                } else {
                    m_state = State::FAILED;
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

        // --- 1. Resource Management Helpers (RAII-like) ---
        struct ResourceGuard {
            AMediaCodec* encoder = nullptr;
            AMediaMuxer* muxer = nullptr;
            ANativeWindow* surface = nullptr;
            EGLDisplay display = EGL_NO_DISPLAY;
            EGLSurface eglSurface = EGL_NO_SURFACE;
            EGLContext eglContext = EGL_NO_CONTEXT;
            FILE* fp = nullptr;
            AMediaFormat* videoFormat = nullptr;

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
                if (muxer) {
                    AMediaMuxer_stop(muxer);
                    AMediaMuxer_delete(muxer);
                }
                if (surface) ANativeWindow_release(surface);
                if (fp) fclose(fp);
                if (videoFormat) AMediaFormat_delete(videoFormat);
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

        status = AMediaCodec_createInputSurface(g.encoder, &g.surface);
        if (status != AMEDIA_OK || !g.surface) return Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Failed to create input surface");

        status = AMediaCodec_start(g.encoder);
        if (status != AMEDIA_OK) return Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Failed to start encoder");

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
        bool muxerStarted = false;

        // --- 5. Render Loop ---
        int64_t totalDurationNs = timeline->getTotalDuration();
        if (totalDurationNs <= 0) return Result::error(ErrorCode::ERR_TIMELINE_NULL, "Timeline duration is zero");

        int64_t frameDurationNs = 1000000000 / m_fps;
        int64_t currentTimeNs = 0;
        int64_t lastChunkStartNs = 0;
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
                    return renderRes;
                }

                if (eglPresentationTimeANDROID) {
                    eglPresentationTimeANDROID(g.display, g.eglSurface, currentTimeNs);
                }
                eglSwapBuffers(g.display, g.eglSurface);

                m_progress = static_cast<float>(currentTimeNs) / static_cast<float>(totalDurationNs);
                if (onProgress) {
                    onProgress(m_progress);
                }

                currentTimeNs += frameDurationNs;

                if (currentTimeNs > totalDurationNs) {
                    AMediaCodec_signalEndOfInputStream(g.encoder);
                    inputEOS = true;
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
                }

                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                    encoderEOS = true;
                }

                AMediaCodec_releaseOutputBuffer(g.encoder, outBufIdx, false);
            } else if (outBufIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (g.videoFormat) AMediaFormat_delete(g.videoFormat);
                g.videoFormat = AMediaCodec_getOutputFormat(g.encoder);
                videoTrackIndex = AMediaMuxer_addTrack(g.muxer, g.videoFormat);
                AMediaMuxer_start(g.muxer);
                muxerStarted = true;
            }
        }

        if (m_chunkDurationNs > 0 && !m_canceled && m_onChunkReady) {
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
