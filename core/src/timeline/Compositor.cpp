#include "../../include/FilterEngine.h"
#include "../../include/timeline/Compositor.h"
#include "../../include/timeline/SubtitleClip.h"
#include "../../include/GLStateManager.h"
#define LOG_TAG "Compositor"
#include "../../include/Log.h"
#include <iostream>
#include <chrono>
#include <numeric>
#include <cstring>

#ifdef __ANDROID__
// GL_EXT_disjoint_timer_query 函数指针
#include <EGL/egl.h>
#include <GLES3/gl3.h>
// 运行时动态获取 EXT 函数指针
typedef void  (*PFNGLGENQUERIESEXTPROC)      (GLsizei n, GLuint *ids);
typedef void  (*PFNGLDELETEQUERIESEXTPROC)   (GLsizei n, const GLuint *ids);
typedef void  (*PFNGLBEGINQUERYEXTPROC)      (GLenum target, GLuint id);
typedef void  (*PFNGLENDQUERYEXTPROC)        (GLenum target);
typedef void  (*PFNGLGETQUERYOBJECTUIVEXTPROC)(GLuint id, GLenum pname, GLuint *params);
#define GL_TIME_ELAPSED_EXT             0x88BF
#define GL_QUERY_RESULT_EXT             0x8866
#define GL_QUERY_RESULT_AVAILABLE_EXT   0x8867
static PFNGLGENQUERIESEXTPROC       s_glGenQueriesEXT        = nullptr;
static PFNGLDELETEQUERIESEXTPROC    s_glDeleteQueriesEXT     = nullptr;
static PFNGLBEGINQUERYEXTPROC       s_glBeginQueryEXT        = nullptr;
static PFNGLENDQUERYEXTPROC         s_glEndQueryEXT          = nullptr;
static PFNGLGETQUERYOBJECTUIVEXTPROC s_glGetQueryObjectuivEXT = nullptr;
#endif

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// Quad vertex source shared by all Compositor programs
// ---------------------------------------------------------------------------
const char* Compositor::s_quadVertSrc = R"(#version 300 es
    layout(location = 0) in vec4 position;
    layout(location = 1) in vec2 texCoord;
    out vec2 v_texCoord;
    void main() {
        gl_Position = position;
        v_texCoord  = texCoord;
    }
)";

Compositor::Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine)
    : m_timeline(timeline), m_filterEngine(engine), m_decoderPool(nullptr) {}

Compositor::~Compositor() {
#ifdef __ANDROID__
    if (m_gpuQuery != 0 && s_glDeleteQueriesEXT) {
        s_glDeleteQueriesEXT(1, &m_gpuQuery);
        m_gpuQuery = 0;
    }
#endif
}

// ---------------------------------------------------------------------------
// DSR: updateDsrScale — 根据近 N 帧平均帧时间自适调整渲染倒率
//
// 策略：
//   * 连续超预算 10%：scale 每帧 ×0.95（下降最快）
//   * 帧时间远低于预算 15%：scale 每帧 ×1.02（缓慢恢复，防所回震荡）
// ---------------------------------------------------------------------------
void Compositor::updateDsrScale(float frameTimeMs) {
    if (!m_dsrEnabled) return;

    if ((int)m_dsrFrameTimes.size() >= m_dsrConfig.sampleWindow) {
        m_dsrFrameTimes.pop_front();
    }
    m_dsrFrameTimes.push_back(frameTimeMs);

    if ((int)m_dsrFrameTimes.size() < 3) return; // 样本不足时不调整

    float sum = std::accumulate(m_dsrFrameTimes.begin(), m_dsrFrameTimes.end(), 0.0f);
    float avg = sum / static_cast<float>(m_dsrFrameTimes.size());
    float targetMs = 1000.0f / std::max(m_dsrConfig.targetFps, 1.0f);

    if (avg > targetMs * 1.10f) {
        // 超预算：降低 scale 加快恢复性能
        m_dsrScale = std::max(m_dsrConfig.minScaleFactor, m_dsrScale * 0.95f);
        LOGD("DSR scale-down: %.2f (avg=%.1fms target=%.1fms)", m_dsrScale, avg, targetMs);
    } else if (avg < targetMs * 0.85f) {
        // 帧时间充裕：缓慢恢复质量
        m_dsrScale = std::min(m_dsrConfig.maxScaleFactor, m_dsrScale * 1.02f);
        LOGD("DSR scale-up:   %.2f (avg=%.1fms target=%.1fms)", m_dsrScale, avg, targetMs);
    }
}

// ---------------------------------------------------------------------------
// GPU timer: GL_EXT_disjoint_timer_query (Android GLES)
// 桌面 GL: glBeginQuery(GL_TIME_ELAPSED)
// ---------------------------------------------------------------------------
bool Compositor::initGpuTimer() {
    if (m_gpuTimerInited) return m_gpuTimerOk;
    m_gpuTimerInited = true;
#ifdef __ANDROID__
    const char* exts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!exts || std::strstr(exts, "GL_EXT_disjoint_timer_query") == nullptr) {
        LOGI("DSR/GPU timer: GL_EXT_disjoint_timer_query not available, using CPU timer");
        m_gpuTimerOk = false;
        return false;
    }
    s_glGenQueriesEXT         = (PFNGLGENQUERIESEXTPROC)       eglGetProcAddress("glGenQueriesEXT");
    s_glDeleteQueriesEXT      = (PFNGLDELETEQUERIESEXTPROC)    eglGetProcAddress("glDeleteQueriesEXT");
    s_glBeginQueryEXT         = (PFNGLBEGINQUERYEXTPROC)       eglGetProcAddress("glBeginQueryEXT");
    s_glEndQueryEXT           = (PFNGLENDQUERYEXTPROC)         eglGetProcAddress("glEndQueryEXT");
    s_glGetQueryObjectuivEXT  = (PFNGLGETQUERYOBJECTUIVEXTPROC)eglGetProcAddress("glGetQueryObjectuivEXT");
    if (!s_glGenQueriesEXT || !s_glBeginQueryEXT || !s_glEndQueryEXT || !s_glGetQueryObjectuivEXT) {
        LOGI("DSR/GPU timer: EXT function pointers unavailable");
        m_gpuTimerOk = false;
        return false;
    }
    s_glGenQueriesEXT(1, &m_gpuQuery);
    m_gpuTimerOk = (m_gpuQuery != 0);
    LOGI("DSR/GPU timer: GL_EXT_disjoint_timer_query enabled (query=%u)", m_gpuQuery);
#else
    // 桌面 OpenGL: 直接使用 CPU 计时即可，GL query 在此环境不需要
    m_gpuTimerOk = false;
#endif
    return m_gpuTimerOk;
}

float Compositor::retrievePendingGpuTimeMs() {
#ifdef __ANDROID__
    if (!m_gpuTimerOk || !m_gpuQueryPending || !s_glGetQueryObjectuivEXT) return -1.0f;
    GLuint available = 0;
    s_glGetQueryObjectuivEXT(m_gpuQuery, GL_QUERY_RESULT_AVAILABLE_EXT, &available);
    if (!available) return -1.0f; // GPU 还未完成
    GLuint resultNs = 0;
    s_glGetQueryObjectuivEXT(m_gpuQuery, GL_QUERY_RESULT_EXT, &resultNs);
    m_gpuQueryPending = false;
    return static_cast<float>(resultNs) * 1e-6f; // ns → ms
#else
    return -1.0f;
#endif
}

// ---------------------------------------------------------------------------
// drawQuad – full-screen triangle-strip (no VAO dependency)
// ---------------------------------------------------------------------------
void Compositor::drawQuad() {
    static const float squareCoords[]  = {-1,-1, 1,-1, -1,1, 1,1};
    static const float textureCoords[] = { 0, 0, 1, 0,  0,1, 1,1};
    GLStateManager::getInstance().enableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, squareCoords);
    GLStateManager::getInstance().enableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// ---------------------------------------------------------------------------
// Program initialisation via RHI
// ---------------------------------------------------------------------------
Result Compositor::initPrograms() {
    Result res = initCopyProgram();
    if (!res.isOk()) return res;
    res = initBlendProgram();
    if (!res.isOk()) return res;
    return initOverlayProgram();
}

Result Compositor::initOverlayProgram() {
    if (m_overlayProgram) return Result::ok();

    auto* rd = m_filterEngine ? m_filterEngine->getRenderDevice() : nullptr;
    if (!rd) return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "No RenderDevice");

    // 叠加层 shader：支持任意大小约束矩形（u_rect = [x0,y0,x1,y1] 归一化）的子区域贴图
    const char* fsrc = R"(#version 300 es
        precision highp float;
        in  vec2 v_texCoord;
        uniform sampler2D texOverlay;
        uniform vec4  u_rect;    // [x0,y0,x1,y1] normalized canvas coords
        uniform float opacity;
        out vec4 fragColor;
        void main() {
            // Map canvas coord to overlay texture coord
            float rx = (v_texCoord.x - u_rect.x) / (u_rect.z - u_rect.x);
            float ry = (v_texCoord.y - u_rect.y) / (u_rect.w - u_rect.y);
            if (rx < 0.0 || rx > 1.0 || ry < 0.0 || ry > 1.0) {
                fragColor = vec4(0.0);
                return;
            }
            vec4 color = texture(texOverlay, vec2(rx, ry));
            fragColor = vec4(color.rgb, color.a * opacity);
        }
    )";

    m_overlayProgram = rd->createShaderProgram(s_quadVertSrc, fsrc);
    if (!m_overlayProgram || !m_overlayProgram->isValid()) {
        m_overlayProgram.reset();
        return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "overlayProgram compile failed");
    }
    return Result::ok();
}

Result Compositor::initCopyProgram() {
    if (m_copyProgram) return Result::ok();

    auto* rd = m_filterEngine ? m_filterEngine->getRenderDevice() : nullptr;
    if (!rd) return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "No RenderDevice");

    const char* fsrc = R"(#version 300 es
        precision highp float;
        in  vec2 v_texCoord;
        uniform sampler2D texForeground;
        uniform float opacity;
        out vec4 fragColor;
        void main() {
            vec4 fg = texture(texForeground, v_texCoord);
            fragColor = vec4(fg.rgb, fg.a * opacity);
        }
    )";

    m_copyProgram = rd->createShaderProgram(s_quadVertSrc, fsrc);
    if (!m_copyProgram || !m_copyProgram->isValid()) {
        m_copyProgram.reset();
        return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "copyProgram compile failed");
    }
    return Result::ok();
}

Result Compositor::copyTexture(const Texture& src, FrameBufferPtr target, float opacity) {
    if (!target) return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Target FBO is null");
    if (src.id == 0) return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Source texture ID is 0");

    target->bind();
    m_copyProgram->bind();
    m_copyProgram->setUniform1i("texForeground", 0);
    m_copyProgram->setUniform1f("opacity", opacity);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, src.id);

    drawQuad();
    target->unbind();
    m_copyProgram->unbind();
    return Result::ok();
}

Result Compositor::initBlendProgram() {
    if (m_blendProgram) return Result::ok();

    auto* rd = m_filterEngine ? m_filterEngine->getRenderDevice() : nullptr;
    if (!rd) return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "No RenderDevice");

    const char* fsrc = R"(#version 300 es
        precision highp float;
        in  vec2 v_texCoord;
        uniform sampler2D texBackground;
        uniform sampler2D texForeground;
        uniform float opacity;
        out vec4 fragColor;
        void main() {
            vec4 bg = texture(texBackground, v_texCoord);
            vec4 fg = texture(texForeground, v_texCoord);
            vec4 blended = fg * fg.a * opacity + bg * (1.0 - fg.a * opacity);
            fragColor = vec4(blended.rgb, max(bg.a, fg.a * opacity));
        }
    )";

    m_blendProgram = rd->createShaderProgram(s_quadVertSrc, fsrc);
    if (!m_blendProgram || !m_blendProgram->isValid()) {
        m_blendProgram.reset();
        return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "blendProgram compile failed");
    }
    return Result::ok();
}

std::shared_ptr<rhi::IShaderProgram> Compositor::getOrCompileTransition(const std::string& name) {
    auto it = m_transitionPrograms.find(name);
    if (it != m_transitionPrograms.end()) return it->second;

    const TransitionDesc* desc = TransitionRegistry::getInstance().getTransition(name);
    if (!desc) {
        LOGE("Compositor: transition '%s' not in registry", name.c_str());
        return nullptr;
    }

    auto* rd = m_filterEngine ? m_filterEngine->getRenderDevice() : nullptr;
    if (!rd) return nullptr;

    auto prog = rd->createShaderProgram(s_quadVertSrc, desc->fragmentGLSL.c_str());
    if (!prog || !prog->isValid()) {
        LOGE("Compositor: failed to compile transition shader '%s'", name.c_str());
        return nullptr;
    }

    m_transitionPrograms[name] = prog;
    return prog;
}

ResultPayload<Texture> Compositor::blendTextures(const Texture& bg, const Texture& fg,
                                                    float opacity, FrameBufferPtr target) {
    if (!target) return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Target FBO is null");

    target->bind();
    m_blendProgram->bind();

    m_blendProgram->setUniform1i("texBackground", 0);
    m_blendProgram->setUniform1i("texForeground",  1);
    m_blendProgram->setUniform1f("opacity",        opacity);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, bg.id);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, fg.id);

    drawQuad();
    target->unbind();
    m_blendProgram->unbind();

    return ResultPayload<Texture>::ok(target->getTexture());
}

ResultPayload<Texture> Compositor::transitionTextures(const Texture& bg, const Texture& fg,
                                                       const std::string& transitionName,
                                                       float progress, FrameBufferPtr target) {
    if (!target) return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Target FBO is null");

    // Lazily compile the transition shader from the registry
    auto prog = getOrCompileTransition(transitionName);
    if (!prog) {
        // Fallback: plain blend
        return blendTextures(bg, fg, progress, target);
    }

    target->bind();
    prog->bind();

    prog->setUniform1i("texBackground", 0);
    prog->setUniform1i("texForeground",  1);
    // Both "opacity" and "progress" uniforms — shader picks whichever it declares
    prog->setUniform1f("progress", progress);
    prog->setUniform1f("opacity",  progress);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, bg.id);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, fg.id);

    drawQuad();
    target->unbind();
    prog->unbind();

    return ResultPayload<Texture>::ok(target->getTexture());
}

Result Compositor::renderFrameAtTime(int64_t timelineNs, FrameBufferPtr outputFb) {
    if (!m_timeline || !m_filterEngine || !outputFb) {
        return Result::error(ErrorCode::ERR_TIMELINE_NULL, "Compositor not properly initialized with Timeline/Engine/FBO");
    }

    // ------------------------------------------------------------------
    // GPU timer: 尝试获取上一帧的 GPU 写入时间（异步，不阻塞渲染线程）
    // ------------------------------------------------------------------
    initGpuTimer();
    float gpuTimeMs = retrievePendingGpuTimeMs(); // -1 表示无法获取

    // 开始新一帧 GPU 查询
#ifdef __ANDROID__
    if (m_gpuTimerOk && s_glBeginQueryEXT) {
        s_glBeginQueryEXT(GL_TIME_ELAPSED_EXT, m_gpuQuery);
        m_gpuQueryPending = true;
    }
#endif

    auto start_time = std::chrono::high_resolution_clock::now();

    // ------------------------------------------------------------------
    // DSR: 计算此帧实际渲染分辨率
    // ------------------------------------------------------------------
    const int renderW = std::max(16, static_cast<int>(std::round(
                            static_cast<float>(outputFb->width())  * m_dsrScale)));
    const int renderH = std::max(16, static_cast<int>(std::round(
                            static_cast<float>(outputFb->height()) * m_dsrScale)));

    Result res = initPrograms();
    if (!res.isOk()) return res;

    m_timeline->getActiveVideoClipsAtTime(timelineNs, m_activeClips);

    if (m_activeClips.empty()) {
        outputFb->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        outputFb->unbind();

#ifdef __ANDROID__
        if (m_gpuTimerOk && s_glEndQueryEXT) s_glEndQueryEXT(GL_TIME_ELAPSED_EXT);
#endif
        auto end_time = std::chrono::high_resolution_clock::now();
        float duration_ms = (gpuTimeMs >= 0.0f) ? gpuTimeMs
            : std::chrono::duration<float, std::milli>(end_time - start_time).count();
        m_metricsCollector.recordFrameTime(duration_ms);
        updateDsrScale(duration_ms);
        return Result::ok();
    }

    if (!m_decoderPool) {
        return Result::error(ErrorCode::ERR_TIMELINE_DECODER_POOL_NULL, "Active clips exist but no decoder pool set");
    }

    Texture accumulatedTexture = {0, 0, 0};
    bool isFirst = true;

    // DSR: ping/pong 使用缩放后的渲染尺寸，copyTexture 最后拉伸至 outputFb 实现上采样
    FrameBufferPtr pingFb = m_filterEngine->getFrameBufferPool()->getFrameBuffer(renderW, renderH);
    FrameBufferPtr pongFb = m_filterEngine->getFrameBufferPool()->getFrameBuffer(renderW, renderH);

    if (!pingFb || !pongFb) {
        return Result::error(ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED, "Failed to allocate ping/pong FBOs");
    }

    LOGD("Compositing %zu clips at %lld", m_activeClips.size(), (long long)timelineNs);

    for (const auto& clip : m_activeClips) {
        int64_t clipRelativeNs = timelineNs - clip->getTimelineIn();
        int64_t localTimeNs = static_cast<int64_t>(clipRelativeNs * clip->getSpeed()) + clip->getEffectiveTrimIn();

        // Clamp local time to effective trim range
        localTimeNs = std::max(clip->getEffectiveTrimIn(), std::min(localTimeNs, clip->getEffectiveTrimOut()));

        ResultPayload<Texture> frameRes = m_decoderPool->getFrame(clip->getId(), localTimeNs);
        if (!frameRes.isOk()) {
            LOGE("Decoder failed for clip %s at %lld: %s", clip->getId().c_str(), (long long)localTimeNs, frameRes.getMessage().c_str());
            return Result::error(frameRes.getErrorCode(), "Decoder failed for clip " + clip->getId() + ": " + frameRes.getMessage());
        }
        Texture fgTex = frameRes.getValue();
        if (fgTex.id == 0) {
            return Result::error(ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED, "Decoder returned invalid texture for clip: " + clip->getId());
        }

        float alpha = clip->getOpacity(clipRelativeNs);
        std::string transitionToUse;  // empty = no transition
        float transitionProgress = 1.0f;

        if (!clip->getInTransitionName().empty() && clipRelativeNs < clip->getInTransitionDurationNs()) {
            transitionToUse = clip->getInTransitionName();
            transitionProgress = static_cast<float>(clipRelativeNs) / clip->getInTransitionDurationNs();
        } else if (!clip->getOutTransitionName().empty()) {
            int64_t clipRemainingNs = clip->getTimelineOut() - timelineNs;
            if (clipRemainingNs < clip->getOutTransitionDurationNs()) {
                transitionToUse = clip->getOutTransitionName();
                transitionProgress = static_cast<float>(clipRemainingNs) / clip->getOutTransitionDurationNs();
            }
        }

        if (isFirst) {
            pingFb->bind();
            glClearColor(0.0, 0.0, 0.0, 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
            pingFb->unbind();

            float initialAlpha = alpha;
            if (!transitionToUse.empty()) {
                initialAlpha *= transitionProgress;
            }
            res = copyTexture(fgTex, pingFb, initialAlpha);
            if (!res.isOk()) return res;
            accumulatedTexture = pingFb->getTexture();
            isFirst = false;
        } else {
            ResultPayload<Texture> blendRes = ResultPayload<Texture>::error(-1, "");
            if (!transitionToUse.empty()) {
                blendRes = transitionTextures(accumulatedTexture, fgTex, transitionToUse, transitionProgress * alpha, pongFb);
            } else {
                blendRes = blendTextures(accumulatedTexture, fgTex, alpha, pongFb);
            }
            if (!blendRes.isOk()) return blendRes;
            accumulatedTexture = blendRes.getValue();
            std::swap(pingFb, pongFb);
        }
    }

    if (accumulatedTexture.id == 0) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Composition produced invalid texture");
    }

    // 滤镜链同样运行在 DSR 分辨率下，节约 GPU 带宽
    auto processResult = m_filterEngine->processFrame(accumulatedTexture, renderW, renderH);
    if (!processResult.isOk()) {
        LOGE("FilterEngine processFrame failed: %s", processResult.getMessage().c_str());
        return Result::error(processResult.getErrorCode(), processResult.getMessage());
    }
    Texture finalTex = processResult.getValue();

    res = copyTexture(finalTex, outputFb);
    if (!res.isOk()) return res;

    // -----------------------------------------------------------------
    // Overlay pass: 字幕 / 贴纸层在视频层之上叠加
    // -----------------------------------------------------------------
    m_timeline->getActiveOverlayClipsAtTime(timelineNs, m_overlayClips);
    if (!m_overlayClips.empty() && m_overlayProgram) {
        outputFb->bind();
        // 开启 Alpha 混合，字幕/贴纸叠在已渲染视频上
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (const auto& clip : m_overlayClips) {
            int64_t relativeNs = timelineNs - clip->getTimelineIn();
            float alpha = clip->getOpacity(relativeNs);

            // 字幕处理
            auto* subClip = dynamic_cast<SubtitleClip*>(clip.get());
            if (subClip && m_textRasterizer) {
                Texture overlayTex = m_textRasterizer->rasterize(
                    subClip->getText(), subClip->style,
                    outputFb->width(), outputFb->height());
                if (overlayTex.id != 0) {
                    float hw = static_cast<float>(overlayTex.width)  / outputFb->width()  * 0.5f;
                    float hh = static_cast<float>(overlayTex.height) / outputFb->height() * 0.5f;
                    float cx = subClip->style.x;
                    float cy = 1.0f - subClip->style.y; // flip Y (GL origin = bottom-left)
                    m_overlayProgram->bind();
                    m_overlayProgram->setUniform1i("texOverlay", 0);
                    m_overlayProgram->setUniform1f("opacity", alpha);
                    float rect[4] = { cx - hw, cy - hh, cx + hw, cy + hh };
                    m_overlayProgram->setUniform4f("u_rect", rect[0], rect[1], rect[2], rect[3]);
                    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
                    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, overlayTex.id);
                    drawQuad();
                    m_overlayProgram->unbind();
                }
            }

            // 贴纸处理（StickerClip：使用 DecoderPool 读取静态图纹理）
            auto* stickerClip = dynamic_cast<StickerClip*>(clip.get());
            if (stickerClip && m_decoderPool) {
                auto frameRes = m_decoderPool->getFrame(stickerClip->getId(), relativeNs);
                if (frameRes.isOk() && frameRes.getValue().id != 0) {
                    Texture stickerTex = frameRes.getValue();
                    float hw = stickerClip->stickerScale * 0.5f;
                    float hh = hw * static_cast<float>(outputFb->width()) / outputFb->height();
                    float cx = stickerClip->centerX;
                    float cy = 1.0f - stickerClip->centerY;
                    m_overlayProgram->bind();
                    m_overlayProgram->setUniform1i("texOverlay", 0);
                    m_overlayProgram->setUniform1f("opacity", alpha);
                    float rect[4] = { cx - hw, cy - hh, cx + hw, cy + hh };
                    m_overlayProgram->setUniform4f("u_rect", rect[0], rect[1], rect[2], rect[3]);
                    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
                    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, stickerTex.id);
                    drawQuad();
                    m_overlayProgram->unbind();
                }
            }
        }

        glDisable(GL_BLEND);
        outputFb->unbind();
        res = Result::ok();
    }

#ifdef __ANDROID__
    if (m_gpuTimerOk && s_glEndQueryEXT) s_glEndQueryEXT(GL_TIME_ELAPSED_EXT);
#endif
    auto end_time = std::chrono::high_resolution_clock::now();
    // 优先使用 GPU 时间（更准确），回退到 CPU 时间
    float duration_ms = (gpuTimeMs >= 0.0f) ? gpuTimeMs
        : std::chrono::duration<float, std::milli>(end_time - start_time).count();
    m_metricsCollector.recordFrameTime(duration_ms);
    updateDsrScale(duration_ms);
    return res;
}

} // namespace timeline
} // namespace video
} // namespace sdk
