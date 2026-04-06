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

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TimelineExporterAndroid", __VA_ARGS__)

namespace sdk {
namespace video {
namespace timeline {

class TimelineExporterAndroid : public TimelineExporter {
public:
    TimelineExporterAndroid() : m_width(0), m_height(0), m_fps(30), m_bitrate(0), m_canceled(false) {}

    ~TimelineExporterAndroid() override {
        cancel();
        if (m_exportThread.joinable()) {
            m_exportThread.join();
        }
    }

    Result configure(const std::string& outputPath, int width, int height, int fps, int bitrate) override {
        m_outputPath = outputPath;
        m_width = width;
        m_height = height;
        m_fps = fps;
        m_bitrate = bitrate;
        return Result::ok();
    }

    void exportAsync(std::shared_ptr<Timeline> timeline,
                     std::shared_ptr<Compositor> compositor,
                     ProgressCallback onProgress,
                     CompletionCallback onComplete) override {

        m_canceled = false;
        m_exportThread = std::thread([this, timeline, compositor, onProgress, onComplete]() {
            Result res = runExport(timeline, compositor, onProgress);
            if (onComplete) {
                onComplete(res);
            }
        });
    }

    void cancel() override {
        m_canceled = true;
    }

private:
    Result runExport(std::shared_ptr<Timeline> timeline,
                     std::shared_ptr<Compositor> compositor,
                     ProgressCallback onProgress) {

        if (!timeline || !compositor) return Result::error(-5001, "Invalid timeline or compositor");

        // 1. Configure MediaCodec Encoder
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, m_width);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, m_height);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, m_bitrate > 0 ? m_bitrate : 4000000);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, m_fps);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 2130708361); // COLOR_FormatSurface

        AMediaCodec* encoder = AMediaCodec_createEncoderByType("video/avc");
        if (!encoder) {
            AMediaFormat_delete(format);
            return Result::error(-5002, "Failed to create encoder");
        }

        media_status_t status = AMediaCodec_configure(encoder, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        AMediaFormat_delete(format);
        if (status != AMEDIA_OK) return Result::error(-5003, "Failed to configure encoder");

        // Create Input Surface from encoder
        ANativeWindow* surface = nullptr;
        status = AMediaCodec_createInputSurface(encoder, &surface);
        if (status != AMEDIA_OK || !surface) return Result::error(-5004, "Failed to create input surface");

        status = AMediaCodec_start(encoder);
        if (status != AMEDIA_OK) return Result::error(-5005, "Failed to start encoder");

        // 2. Setup Headless EGL Context bounded to Encoder's InputSurface
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(display, 0, 0);

        const EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_RECORDABLE_ANDROID, 1, // Crucial for MediaCodec compatibility
            EGL_NONE
        };

        EGLConfig config;
        EGLint numConfigs;
        eglChooseConfig(display, attribs, &config, 1, &numConfigs);

        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        // Share context? In a real system, we might want to share the context with the main GL thread
        // to avoid reloading textures. For this isolation, we create a fresh one and assume the compositor
        // reloads what it needs (or the caller passed a shared context. Assuming EGL_NO_CONTEXT for skeleton).
        EGLContext eglContext = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);

        EGLSurface eglSurface = eglCreateWindowSurface(display, config, surface, nullptr);
        eglMakeCurrent(display, eglSurface, eglSurface, eglContext);

        // eglPresentationTimeANDROID extension for setting encoder timestamps
        auto eglPresentationTimeANDROID = (PFNEGLPRESENTATIONTIMEANDROIDPROC)eglGetProcAddress("eglPresentationTimeANDROID");

        // 3. Setup Muxer
        FILE* fp = fopen(m_outputPath.c_str(), "wb");
        if (!fp) return Result::error(-5006, "Failed to open output file");
        int fd = fileno(fp);

        AMediaMuxer* muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        ssize_t videoTrackIndex = -1;
        bool muxerStarted = false;

        // 4. Render Loop
        int64_t totalDurationUs = timeline->getTotalDuration();
        int64_t frameDurationUs = 1000000 / m_fps;
        int64_t currentTimeUs = 0;
        bool encoderEOS = false;

        while (!encoderEOS && !m_canceled) {
            // Push frame
            if (currentTimeUs <= totalDurationUs) {
                glViewport(0, 0, m_width, m_height);
                glClearColor(0,0,0,1);
                glClear(GL_COLOR_BUFFER_BIT);

                // Compositor draws directly to the default framebuffer (EGLSurface -> MediaCodec)
                // We create a dummy wrapper for ID 0
                FrameBufferPtr screenFb = std::make_shared<FrameBuffer>(m_width, m_height, 0);
                compositor->renderFrameAtTime(currentTimeUs, screenFb);

                if (eglPresentationTimeANDROID) {
                    eglPresentationTimeANDROID(display, eglSurface, currentTimeUs * 1000); // ns
                }
                eglSwapBuffers(display, eglSurface);

                if (onProgress) {
                    onProgress(static_cast<float>(currentTimeUs) / static_cast<float>(totalDurationUs));
                }

                currentTimeUs += frameDurationUs;

                if (currentTimeUs > totalDurationUs) {
                    AMediaCodec_signalEndOfInputStream(encoder);
                }
            }

            // Drain encoder
            AMediaCodecBufferInfo info;
            ssize_t outBufIdx = AMediaCodec_dequeueOutputBuffer(encoder, &info, 10000);
            if (outBufIdx >= 0) {
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                    AMediaCodec_releaseOutputBuffer(encoder, outBufIdx, false);
                    continue;
                }

                if (info.size > 0 && muxerStarted) {
                    size_t bufSize = 0;
                    uint8_t* buf = AMediaCodec_getOutputBuffer(encoder, outBufIdx, &bufSize);
                    AMediaMuxer_writeSampleData(muxer, videoTrackIndex, buf, &info);
                }

                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                    encoderEOS = true;
                }

                AMediaCodec_releaseOutputBuffer(encoder, outBufIdx, false);
            } else if (outBufIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* newFormat = AMediaCodec_getOutputFormat(encoder);
                videoTrackIndex = AMediaMuxer_addTrack(muxer, newFormat);
                AMediaMuxer_start(muxer);
                muxerStarted = true;
                AMediaFormat_delete(newFormat);
            }
        }

        // Cleanup
        if (muxerStarted) {
            AMediaMuxer_stop(muxer);
        }
        AMediaMuxer_delete(muxer);
        fclose(fp);

        AMediaCodec_stop(encoder);
        AMediaCodec_delete(encoder);
        ANativeWindow_release(surface);

        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, eglSurface);
        eglDestroyContext(display, eglContext);

        if (m_canceled) {
            remove(m_outputPath.c_str());
            return Result::error(-5007, "Export canceled by user");
        }

        return Result::ok();
    }

    std::string m_outputPath;
    int m_width;
    int m_height;
    int m_fps;
    int m_bitrate;

    std::atomic<bool> m_canceled;
    std::thread m_exportThread;
};

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // __ANDROID__
