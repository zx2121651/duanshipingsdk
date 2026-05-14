/**
 * FFmpegVideoDecoder.cpp
 *
 * 基于 FFmpeg libavcodec 的软件视频解码器。
 * 当 AMediaCodec 硬件解码失败（hwFailed）后由 DecoderPool 自动启用。
 *
 * 解码与上传策略（GPU YUV→RGB）：
 *
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ 像素格式          │ GL 上传路径       │ CPU 开销         │
 *   ├─────────────────────────────────────────────────────────┤
 *   │ YUV420P / YUVJ420P│ 3-plane I420 直传 │ 零（最常见）    │
 *   │ NV12              │ 2-plane NV12 直传 │ 零              │
 *   │ 其他（10bit 等）  │ swscale → I420   │ 少量（格式转换） │
 *   └─────────────────────────────────────────────────────────┘
 *
 *   avformat_open_input → avcodec_open2
 *   getFrameAt() → av_seek_frame → avcodec_receive_frame
 *     → 上传 Y/U/V 或 Y/UV plane → FBO YUV→RGB shader → Texture
 *
 * 内存对比（1080p 帧）：
 *   旧 RGBA 路径 ：1920×1080×4 ≈ 8.3 MB / 帧
 *   新 I420 路径 ：1920×1080×1.5 ≈ 3.1 MB / 帧（节省 63%）
 *
 * 编译守护：
 *   仅当 HAS_FFMPEG_DECODER 宏被定义时编译。
 */

#ifdef HAS_FFMPEG_DECODER

#include "../../include/timeline/VideoDecoder.h"
#include "../../include/GLStateManager.h"
#define LOG_TAG "FFmpegVideoDecoder"
#include "../../include/Log.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <GLES3/gl3.h>
#include <string>
#include <vector>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// YUV 上传模式
// ---------------------------------------------------------------------------
enum class YUVUploadMode {
    I420,            // YUV420P / YUVJ420P — 3 planes: Y, U, V
    NV12,            // NV12 — 2 planes: Y, UV-interleaved
    SWSCALE_TO_I420, // 其他格式，通过 swscale 转为 I420 后上传
};

static YUVUploadMode detectUploadMode(AVPixelFormat fmt) {
    switch (fmt) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            return YUVUploadMode::I420;
        case AV_PIX_FMT_NV12:
            return YUVUploadMode::NV12;
        default:
            return YUVUploadMode::SWSCALE_TO_I420;
    }
}

// ---------------------------------------------------------------------------
// GLSL: 通用 VS（位置 + UV）
// ---------------------------------------------------------------------------
static const char* kYUVVertSrc = R"(#version 300 es
    layout(location = 0) in vec4 position;
    layout(location = 1) in vec2 texCoord;
    out vec2 v_uv;
    void main() {
        gl_Position = position;
        v_uv = texCoord;
    }
)";

// ---------------------------------------------------------------------------
// GLSL: I420 片元着色器（3 sampler: Y, U, V）
// BT.601 有限范围 (studio swing) YUV→RGB
// ---------------------------------------------------------------------------
static const char* kI420FragSrc = R"(#version 300 es
    precision highp float;
    in vec2 v_uv;
    uniform sampler2D texY;
    uniform sampler2D texU;
    uniform sampler2D texV;
    out vec4 fragColor;
    void main() {
        float y = texture(texY, v_uv).r - 0.0625;
        float u = texture(texU, v_uv).r - 0.5;
        float v = texture(texV, v_uv).r - 0.5;
        float r = clamp(1.164 * y + 1.596 * v,         0.0, 1.0);
        float g = clamp(1.164 * y - 0.391 * u - 0.813 * v, 0.0, 1.0);
        float b = clamp(1.164 * y + 2.018 * u,         0.0, 1.0);
        fragColor = vec4(r, g, b, 1.0);
    }
)";

// ---------------------------------------------------------------------------
// GLSL: NV12 片元着色器（2 sampler: Y, UV-interleaved）
// ---------------------------------------------------------------------------
static const char* kNV12FragSrc = R"(#version 300 es
    precision highp float;
    in vec2 v_uv;
    uniform sampler2D texY;
    uniform sampler2D texUV;
    out vec4 fragColor;
    void main() {
        float y  = texture(texY,  v_uv).r - 0.0625;
        vec2  uv = texture(texUV, v_uv).rg - vec2(0.5, 0.5);
        float r = clamp(1.164 * y + 1.596 * uv.y,               0.0, 1.0);
        float g = clamp(1.164 * y - 0.391 * uv.x - 0.813 * uv.y, 0.0, 1.0);
        float b = clamp(1.164 * y + 2.018 * uv.x,               0.0, 1.0);
        fragColor = vec4(r, g, b, 1.0);
    }
)";

// ---------------------------------------------------------------------------
// FFmpegVideoDecoder
// ---------------------------------------------------------------------------
class FFmpegVideoDecoder : public VideoDecoder {
public:
    FFmpegVideoDecoder() = default;
    ~FFmpegVideoDecoder() override { close(); }

    // -----------------------------------------------------------------------
    // open(): avformat_open_input → avcodec_open2 → 初始化 swscale（仅非 I420/NV12）
    // -----------------------------------------------------------------------
    Result open(const std::string& filePath) override {
        m_filePath = filePath;

        m_fmtCtx = avformat_alloc_context();
        if (!m_fmtCtx)
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: avformat_alloc_context failed");

        if (avformat_open_input(&m_fmtCtx, filePath.c_str(), nullptr, nullptr) < 0) {
            m_fmtCtx = nullptr; // avformat_open_input already freed it on failure
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: cannot open " + filePath);
        }

        if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
            close();
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: cannot find stream info: " + filePath);
        }

        m_videoStreamIdx = -1;
        for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
            if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                m_videoStreamIdx = static_cast<int>(i);
                break;
            }
        }
        if (m_videoStreamIdx < 0) {
            close();
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: no video stream in " + filePath);
        }

        AVStream* stream = m_fmtCtx->streams[m_videoStreamIdx];
        m_timeBase = stream->time_base;
        m_width    = stream->codecpar->width;
        m_height   = stream->codecpar->height;
        m_srcFmt   = static_cast<AVPixelFormat>(stream->codecpar->format);

        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            close();
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: no decoder for codec_id");
        }

        m_codecCtx = avcodec_alloc_context3(codec);
        if (!m_codecCtx) {
            close();
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: avcodec_alloc_context3 failed");
        }

        if (avcodec_parameters_to_context(m_codecCtx, stream->codecpar) < 0) {
            close();
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: avcodec_parameters_to_context failed");
        }

        // 帧级并行解码（适合 H.264/H.265）
        m_codecCtx->thread_count = 2;
        m_codecCtx->thread_type  = FF_THREAD_FRAME;

        if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
            close();
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: avcodec_open2 failed for " + filePath);
        }

        // codec 实际输出格式（可能与 codecpar->format 不同）
        AVPixelFormat actualFmt = m_codecCtx->pix_fmt != AV_PIX_FMT_NONE
                                    ? m_codecCtx->pix_fmt : m_srcFmt;
        m_uploadMode = detectUploadMode(actualFmt);

        // 只有非直传格式才需要 swscale（转为 I420）
        if (m_uploadMode == YUVUploadMode::SWSCALE_TO_I420) {
            LOGI("FFmpegVideoDecoder: pix_fmt=%d needs swscale→I420", (int)actualFmt);
            m_swsCtx = sws_getContext(
                m_width, m_height, actualFmt,
                m_width, m_height, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!m_swsCtx) {
                close();
                return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                    "FFmpegVideoDecoder: sws_getContext failed");
            }

            // 预分配 I420 中间帧
            m_i420Frame = av_frame_alloc();
            if (!m_i420Frame) {
                close();
                return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                    "FFmpegVideoDecoder: av_frame_alloc (i420) failed");
            }
            int bufSz = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, m_width, m_height, 1);
            m_i420Buffer.resize(static_cast<size_t>(bufSz));
            av_image_fill_arrays(m_i420Frame->data, m_i420Frame->linesize,
                                 m_i420Buffer.data(), AV_PIX_FMT_YUV420P,
                                 m_width, m_height, 1);
        }

        m_frame  = av_frame_alloc();
        m_packet = av_packet_alloc();
        if (!m_frame || !m_packet) {
            close();
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                "FFmpegVideoDecoder: av_alloc failed");
        }

        m_isOpen = true;
        const char* modeName[] = {"I420", "NV12", "swscale→I420"};
        LOGI("FFmpegVideoDecoder opened: %s  %dx%d  codec=%s  upload=%s",
             filePath.c_str(), m_width, m_height, codec->name,
             modeName[static_cast<int>(m_uploadMode)]);
        return Result::ok();
    }

    // -----------------------------------------------------------------------
    // seekExact()
    // -----------------------------------------------------------------------
    Result seekExact(int64_t timeNs) override {
        if (!m_isOpen || !m_codecCtx || !m_fmtCtx)
            return Result::error(ErrorCode::ERR_DECODER_SEEK_FAILED,
                "FFmpegVideoDecoder: not open or null context");

        int64_t ts = av_rescale_q(timeNs / 1000,
                                   AVRational{1, AV_TIME_BASE},
                                   m_timeBase);
        int ret = av_seek_frame(m_fmtCtx, m_videoStreamIdx, ts, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            char err[64]; av_strerror(ret, err, sizeof(err));
            LOGE("FFmpegVideoDecoder: seek failed at %lld ns: %s", (long long)timeNs, err);
            return Result::error(ErrorCode::ERR_DECODER_SEEK_FAILED,
                std::string("FFmpegVideoDecoder: seek failed: ") + err);
        }
        avcodec_flush_buffers(m_codecCtx);
        m_lastSeekTimeNs = timeNs;
        return Result::ok();
    }

    // -----------------------------------------------------------------------
    // getFrameAt(): 解码到目标帧 → GPU 上传 → 返回 Texture
    // -----------------------------------------------------------------------
    ResultPayload<Texture> getFrameAt(int64_t timeNs) override {
        if (!m_isOpen)
            return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED,
                "FFmpegVideoDecoder: not open");

        ensureGLResources();

        // 解码循环
        constexpr int kMaxPackets = 512;
        int  packetCount = 0;
        bool found = false;

        while (!found && packetCount++ < kMaxPackets) {
            int ret = av_read_frame(m_fmtCtx, m_packet);
            if (ret < 0) {
                avcodec_send_packet(m_codecCtx, nullptr);
                if (avcodec_receive_frame(m_codecCtx, m_frame) >= 0)
                    found = true;
                break;
            }
            if (m_packet->stream_index != m_videoStreamIdx) {
                av_packet_unref(m_packet); continue;
            }
            if (avcodec_send_packet(m_codecCtx, m_packet) < 0) {
                av_packet_unref(m_packet); continue;
            }
            av_packet_unref(m_packet);

            while (avcodec_receive_frame(m_codecCtx, m_frame) >= 0) {
                int64_t ptsNs = av_rescale_q(m_frame->pts,
                                              m_timeBase,
                                              AVRational{1, 1000000000});
                if (ptsNs >= timeNs - 33000000LL) { // ±1 帧容差（33ms@30fps）
                    found = true; break;
                }
                av_frame_unref(m_frame);
            }
        }

        if (!found) {
            LOGW("FFmpegVideoDecoder: frame not found at %lld ns", (long long)timeNs);
            if (m_textureId != 0)
                return ResultPayload<Texture>::ok(
                    {m_textureId, (uint32_t)m_width, (uint32_t)m_height});
            return ResultPayload<Texture>::error(ErrorCode::ERR_DECODER_FRAME_DROP,
                "FFmpegVideoDecoder: no frame at target time");
        }

        // 如需格式转换，先通过 swscale 写入 I420 中间帧
        AVFrame* uploadFrame = m_frame;
        if (m_uploadMode == YUVUploadMode::SWSCALE_TO_I420) {
            sws_scale(m_swsCtx,
                      m_frame->data, m_frame->linesize, 0, m_height,
                      m_i420Frame->data, m_i420Frame->linesize);
            uploadFrame = m_i420Frame;
        }

        // GPU 上传 + FBO YUV→RGB
        uploadYUVAndRender(uploadFrame);
        av_frame_unref(m_frame);

        return ResultPayload<Texture>::ok(
            {m_textureId, (uint32_t)m_width, (uint32_t)m_height});
    }

    // -----------------------------------------------------------------------
    // close()
    // -----------------------------------------------------------------------
    void close() override {
        m_isOpen = false;
        releaseGLResources();

        if (m_codecCtx)  { avcodec_free_context(&m_codecCtx); }
        if (m_fmtCtx)    { avformat_close_input(&m_fmtCtx);   }
        if (m_frame)     { av_frame_free(&m_frame);      }
        if (m_i420Frame) { av_frame_free(&m_i420Frame);  }
        if (m_packet)    { av_packet_free(&m_packet);    }
        if (m_swsCtx)    { sws_freeContext(m_swsCtx);    m_swsCtx = nullptr; }

        m_i420Buffer.clear();
        LOGI("FFmpegVideoDecoder closed: %s", m_filePath.c_str());
    }

private:
    // ---- FFmpeg 上下文 ----
    AVFormatContext* m_fmtCtx         = nullptr;
    AVCodecContext*  m_codecCtx       = nullptr;
    AVFrame*         m_frame          = nullptr;
    AVFrame*         m_i420Frame      = nullptr; // swscale 中间帧（仅非 I420/NV12 时使用）
    AVPacket*        m_packet         = nullptr;
    SwsContext*      m_swsCtx         = nullptr; // 格式转换（不做缩放）
    AVRational       m_timeBase       = {1, 1000000};
    AVPixelFormat    m_srcFmt         = AV_PIX_FMT_NONE;
    YUVUploadMode    m_uploadMode     = YUVUploadMode::I420;
    int              m_videoStreamIdx = -1;
    int              m_width  = 0;
    int              m_height = 0;
    int64_t          m_lastSeekTimeNs = 0;
    std::vector<uint8_t> m_i420Buffer;
    bool             m_isOpen  = false;
    std::string      m_filePath;

    // ---- GL 资源 ----
    GLuint m_textureId = 0; // 输出 RGBA 纹理（FBO 色彩附件）
    GLuint m_fbo       = 0;
    // I420: yTex + uTex + vTex; NV12: yTex + uvTex (vTex=0)
    GLuint m_yTex  = 0;
    GLuint m_uTex  = 0; // I420 U plane / NV12 UV plane (reuse slot)
    GLuint m_vTex  = 0; // I420 V plane 专用
    GLuint m_i420Program = 0;
    GLuint m_nv12Program = 0;
    bool   m_glInited    = false;

    // -----------------------------------------------------------------------
    // 编译单个 shader program
    // -----------------------------------------------------------------------
    static GLuint buildProgram(const char* vsSrc, const char* fsSrc) {
        auto compile = [](GLenum type, const char* src) -> GLuint {
            GLuint sh = glCreateShader(type);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            return sh;
        };
        GLuint vs = compile(GL_VERTEX_SHADER,   vsSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs); glDeleteShader(fs);
        return prog;
    }

    // -----------------------------------------------------------------------
    // 初始化 GL 资源（必须在 GL 线程调用）
    // -----------------------------------------------------------------------
    void ensureGLResources() {
        if (m_glInited || m_width <= 0 || m_height <= 0) return;
        m_glInited = true;

        auto allocTex = [&](GLuint& id, GLint fmt, int w, int h) {
            glGenTextures(1, &id);
            GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0,
                         fmt == GL_RG8 ? GL_RG : GL_RED, GL_UNSIGNED_BYTE, nullptr);
        };

        // Y plane (共用)
        allocTex(m_yTex, GL_R8, m_width, m_height);

        if (m_uploadMode == YUVUploadMode::NV12) {
            // NV12: UV 平面 = width/2 × height/2, GL_RG8
            allocTex(m_uTex, GL_RG8, m_width / 2, m_height / 2);
            m_nv12Program = buildProgram(kYUVVertSrc, kNV12FragSrc);
        } else {
            // I420 (直传 或 swscale→I420)
            allocTex(m_uTex, GL_R8, m_width / 2, m_height / 2);
            allocTex(m_vTex, GL_R8, m_width / 2, m_height / 2);
            m_i420Program = buildProgram(kYUVVertSrc, kI420FragSrc);
        }

        // 输出 RGBA 纹理 + FBO
        glGenTextures(1, &m_textureId);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glGenFramebuffers(1, &m_fbo);
        GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_textureId, 0);
        GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // -----------------------------------------------------------------------
    // 上传 YUV plane(s) 到 GL 纹理，然后通过 FBO shader 渲染为 RGBA
    // -----------------------------------------------------------------------
    void uploadYUVAndRender(AVFrame* frame) {
        // GL_UNPACK_ROW_LENGTH 允许直接使用 FFmpeg 带 padding 的 linesize，无需额外 memcpy
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        // --- 上传 Y plane ---
        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_yTex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height,
                        GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);

        if (m_uploadMode == YUVUploadMode::NV12) {
            // --- 上传 NV12 UV 平面 (interleaved) ---
            GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
            GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_uTex);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1] / 2);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width / 2, m_height / 2,
                            GL_RG, GL_UNSIGNED_BYTE, frame->data[1]);
        } else {
            // --- 上传 I420 U 平面 ---
            GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
            GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_uTex);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width / 2, m_height / 2,
                            GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);

            // --- 上传 I420 V 平面 ---
            GLStateManager::getInstance().activeTexture(GL_TEXTURE2);
            GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_vTex);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width / 2, m_height / 2,
                            GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);
        }

        // 恢复默认 row length
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        // --- FBO pass: YUV → RGBA ---
        GLint oldFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
        GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, m_width, m_height);

        GLuint prog = (m_uploadMode == YUVUploadMode::NV12) ? m_nv12Program : m_i420Program;
        GLStateManager::getInstance().useProgram(prog);

        glUniform1i(glGetUniformLocation(prog, "texY"), 0);
        if (m_uploadMode == YUVUploadMode::NV12) {
            glUniform1i(glGetUniformLocation(prog, "texUV"), 1);
        } else {
            glUniform1i(glGetUniformLocation(prog, "texU"), 1);
            glUniform1i(glGetUniformLocation(prog, "texV"), 2);
        }

        static const float kPos[] = {-1,-1, 1,-1, -1,1, 1,1};
        static const float kUV[]  = { 0, 0, 1, 0,  0,1, 1,1};
        GLStateManager::getInstance().enableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kPos);
        GLStateManager::getInstance().enableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, kUV);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, oldFBO);
    }

    // -----------------------------------------------------------------------
    // 释放 GL 资源
    // -----------------------------------------------------------------------
    void releaseGLResources() {
        GLuint textures[] = {m_textureId, m_yTex, m_uTex, m_vTex};
        for (GLuint& t : textures) {
            if (t != 0) { glDeleteTextures(1, &t); t = 0; }
        }
        if (m_fbo != 0) {
            glDeleteFramebuffers(1, &m_fbo); m_fbo = 0;
        }
        if (m_i420Program != 0) {
            glDeleteProgram(m_i420Program); m_i420Program = 0;
        }
        if (m_nv12Program != 0) {
            glDeleteProgram(m_nv12Program); m_nv12Program = 0;
        }
        m_glInited = false;
    }
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) std::shared_ptr<VideoDecoder> createSoftwareDecoder_FFmpeg() {
#else
std::shared_ptr<VideoDecoder> createSoftwareDecoder_FFmpeg() {
#endif
    return std::make_shared<FFmpegVideoDecoder>();
}

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // HAS_FFMPEG_DECODER
