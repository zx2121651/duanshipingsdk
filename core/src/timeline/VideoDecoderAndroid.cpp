#include "../../include/timeline/VideoDecoder.h"

#ifdef __ANDROID__
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/log.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VideoDecoderAndroid", __VA_ARGS__)

namespace sdk {
namespace video {
namespace timeline {

class VideoDecoderAndroid : public VideoDecoder {
public:
    VideoDecoderAndroid() : m_extractor(nullptr), m_codec(nullptr), m_textureId(0),
        m_width(0), m_height(0), m_yTexture(0), m_uvTexture(0), m_fbo(0), m_yuvProgram(0),
        m_running(false) {}

    ~VideoDecoderAndroid() override {
        close();
    }

    Result open(const std::string& filePath) override {
        m_extractor = AMediaExtractor_new();
        if (!m_extractor) return Result::error(-4001, "Failed to create AMediaExtractor");

        media_status_t err = AMediaExtractor_setDataSource(m_extractor, filePath.c_str());
        if (err != AMEDIA_OK) {
            LOGE("Failed to set data source: %s", filePath.c_str());
            return Result::error(-4002, "Failed to set data source");
        }

        int numTracks = AMediaExtractor_getTrackCount(m_extractor);
        AMediaFormat* format = nullptr;
        const char* mime = nullptr;

        for (int i = 0; i < numTracks; i++) {
            format = AMediaExtractor_getTrackFormat(m_extractor, i);
            AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
            if (mime && strncmp(mime, "video/", 6) == 0) {
                AMediaExtractor_selectTrack(m_extractor, i);
                break;
            }
            AMediaFormat_delete(format);
            format = nullptr;
        }

        if (!format) return Result::error(-4003, "No video track found");

        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &m_width);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &m_height);

        m_codec = AMediaCodec_createDecoderByType(mime);
        if (!m_codec) {
            AMediaFormat_delete(format);
            return Result::error(-4004, "Failed to create codec");
        }

        err = AMediaCodec_configure(m_codec, format, nullptr, nullptr, 0); // ByteBuffer mode
        AMediaFormat_delete(format);

        if (err != AMEDIA_OK) return Result::error(-4005, "Failed to configure codec");

        err = AMediaCodec_start(m_codec);
        if (err != AMEDIA_OK) return Result::error(-4006, "Failed to start codec");

        m_running = true;
        m_decodeThread = std::thread(&VideoDecoderAndroid::decodeLoop, this);

        return Result::ok();
    }

    void decodeLoop() {
        bool sawInputEOS = false;
        bool sawOutputEOS = false;

        while (m_running && !sawOutputEOS) {
            // Check queue size
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_queueCv.wait(lock, [this]() { return m_frameQueue.size() < 3 || !m_running; });
            }
            if (!m_running) break;

            if (!sawInputEOS) {
                ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(m_codec, 10000);
                if (bufIdx >= 0) {
                    size_t bufSize = 0;
                    uint8_t* buf = AMediaCodec_getInputBuffer(m_codec, bufIdx, &bufSize);
                    ssize_t sampleSize = AMediaExtractor_readSampleData(m_extractor, buf, bufSize);

                    if (sampleSize < 0) {
                        sampleSize = 0;
                        sawInputEOS = true;
                    }

                    int64_t presentationTimeUs = AMediaExtractor_getSampleTime(m_extractor);
                    AMediaCodec_queueInputBuffer(m_codec, bufIdx, 0, sampleSize, presentationTimeUs,
                                                 sawInputEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);

                    AMediaExtractor_advance(m_extractor);
                }
            }

            AMediaCodecBufferInfo info;
            ssize_t status = AMediaCodec_dequeueOutputBuffer(m_codec, &info, 10000);

            if (status >= 0) {
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                    sawOutputEOS = true;
                }

                if (info.size > 0) {
                    size_t bufSize = 0;
                    uint8_t* buf = AMediaCodec_getOutputBuffer(m_codec, status, &bufSize);

                    std::shared_ptr<FrameBufferPacket> packet = std::make_shared<FrameBufferPacket>();
                    packet->ptsNs = info.presentationTimeUs * 1000;
                    packet->width = m_width;
                    packet->height = m_height;
                    packet->data.assign(buf, buf + info.size);

                    {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_frameQueue.push(packet);
                    }

                    AMediaCodec_releaseOutputBuffer(m_codec, status, false);
                } else {
                    AMediaCodec_releaseOutputBuffer(m_codec, status, false);
                }
            }
        }
    }

    void initYUVProgram() {
        if (m_yuvProgram != 0) return;

        const char* vsrc = R"(#version 300 es
            layout(location = 0) in vec4 position;
            layout(location = 1) in vec2 texCoord;
            out vec2 v_texCoord;
            void main() {
                gl_Position = position;
                v_texCoord = texCoord;
            }
        )";

        const char* fsrc = R"(#version 300 es
            precision highp float;
            in vec2 v_texCoord;
            uniform sampler2D texY;
            uniform sampler2D texUV;
            out vec4 fragColor;

            void main() {
                float y = texture(texY, v_texCoord).r;
                vec2 uv = texture(texUV, v_texCoord).rg - vec2(0.5, 0.5);

                float r = y + 1.402 * uv.y;
                float g = y - 0.344 * uv.x - 0.714 * uv.y;
                float b = y + 1.772 * uv.x;

                fragColor = vec4(r, g, b, 1.0);
            }
        )";

        auto compile = [](GLenum type, const char* s) {
            GLuint sh = glCreateShader(type); glShaderSource(sh, 1, &s, NULL); glCompileShader(sh); return sh;
        };
        GLuint vs = compile(GL_VERTEX_SHADER, vsrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
        m_yuvProgram = glCreateProgram();
        glAttachShader(m_yuvProgram, vs); glAttachShader(m_yuvProgram, fs);
        glLinkProgram(m_yuvProgram);
        glDeleteShader(vs); glDeleteShader(fs);

        glGenTextures(1, &m_yTexture);
        glBindTexture(GL_TEXTURE_2D, m_yTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &m_uvTexture);
        glBindTexture(GL_TEXTURE_2D, m_uvTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &m_textureId);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textureId, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    Texture getFrameAt(int64_t timeNs) override {
        initYUVProgram();

        std::shared_ptr<FrameBufferPacket> targetPacket = nullptr;

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            // Drop old frames
            while (!m_frameQueue.empty() && m_frameQueue.front()->ptsNs < timeNs - 30000000) {
                m_frameQueue.pop();
                m_queueCv.notify_one();
            }

            if (!m_frameQueue.empty()) {
                targetPacket = m_frameQueue.front();
            }
        }

        if (targetPacket) {
            int ySize = m_width * m_height;
            int uvSize = m_width * m_height / 2;

            if (targetPacket->data.size() >= ySize + uvSize) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_yTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_width, m_height, 0, GL_RED, GL_UNSIGNED_BYTE, targetPacket->data.data());

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m_uvTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, m_width / 2, m_height / 2, 0, GL_RG, GL_UNSIGNED_BYTE, targetPacket->data.data() + ySize);

                GLint oldFBO;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);

                glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
                glViewport(0, 0, m_width, m_height);

                glUseProgram(m_yuvProgram);
                glUniform1i(glGetUniformLocation(m_yuvProgram, "texY"), 0);
                glUniform1i(glGetUniformLocation(m_yuvProgram, "texUV"), 1);

                static const float squareCoords[] = {-1, -1, 1, -1, -1, 1, 1, 1};
                static const float textureCoords[] = {0, 0, 1, 0, 0, 1, 1, 1};

                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, squareCoords);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);

                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
            }
            return {m_textureId, (uint32_t)m_width, (uint32_t)m_height};
        }

        return {m_textureId, (uint32_t)m_width, (uint32_t)m_height}; // Return last known valid or 0
    }

    void close() override {
        m_running = false;
        m_queueCv.notify_all();
        if (m_decodeThread.joinable()) {
            m_decodeThread.join();
        }

        if (m_codec) {
            AMediaCodec_stop(m_codec);
            AMediaCodec_delete(m_codec);
            m_codec = nullptr;
        }
        if (m_extractor) {
            AMediaExtractor_delete(m_extractor);
            m_extractor = nullptr;
        }
        if (m_textureId != 0) {
            glDeleteTextures(1, &m_textureId);
            m_textureId = 0;
        }
        if (m_yTexture != 0) {
            glDeleteTextures(1, &m_yTexture);
            m_yTexture = 0;
        }
        if (m_uvTexture != 0) {
            glDeleteTextures(1, &m_uvTexture);
            m_uvTexture = 0;
        }
        if (m_fbo != 0) {
            glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }
        if (m_yuvProgram != 0) {
            glDeleteProgram(m_yuvProgram);
            m_yuvProgram = 0;
        }

        while (!m_frameQueue.empty()) m_frameQueue.pop();
    }

private:
    AMediaExtractor* m_extractor;
    AMediaCodec* m_codec;
    GLuint m_textureId;
    int32_t m_width;
    int32_t m_height;

    GLuint m_yTexture;
    GLuint m_uvTexture;
    GLuint m_fbo;
    GLuint m_yuvProgram;

    std::atomic<bool> m_running;
    std::thread m_decodeThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::queue<std::shared_ptr<FrameBufferPacket>> m_frameQueue;
};

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // __ANDROID__

// Platform Decoder Factory Implementation
std::shared_ptr<VideoDecoder> createPlatformDecoder() {
    return std::make_shared<VideoDecoderAndroid>();
}
