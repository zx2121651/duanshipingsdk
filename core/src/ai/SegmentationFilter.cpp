/**
 * SegmentationFilter.cpp
 *
 * 人像分割合成滤镜。
 *
 * processFrame() 流程（BLUR_BG 模式示意）：
 *
 *   inputTexture ──┬──────────────────────────────────────────► FBO → merged
 *                  │                                              ▲
 *                  └── GaussianBlur(blurStrength) ─► blurTex ───┘
 *                                                    maskTex ───►(shader)
 *
 * REPLACE_BG 模式：
 *   inputTexture + maskTex + bgColor → 单 pass 合成
 */

#include "../../include/ai/SegmentationFilter.h"
#include "../../include/GLStateManager.h"
#include "../../include/Filters.h"
#include "../../include/rhi/IRenderDevice.h"

#define LOG_TAG "SegmentationFilter"
#include "../../include/Log.h"

namespace sdk {
namespace video {

// ---------------------------------------------------------------------------
// 内联 shader 源码（与 assets/shaders/ 中文件内容保持一致）
// ---------------------------------------------------------------------------
static const char* kSegVertSrc = R"(#version 300 es
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texCoord;
out vec2 v_texCoord;
void main() {
    gl_Position = position;
    v_texCoord  = texCoord;
}
)";

static const char* kSegFragSrc = R"(#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D texInput;
uniform sampler2D texMask;
uniform sampler2D texBgImage;
uniform int       u_mode;
uniform vec4      u_bgColor;
uniform float     u_edgeSoften;
out vec4 fragColor;
void main() {
    vec4  orig = texture(texInput, v_texCoord);
    float fg   = texture(texMask,  v_texCoord).r;
    float softRange = u_edgeSoften * 0.15;
    float alpha = smoothstep(0.5 - softRange, 0.5 + softRange, fg);
    if (u_mode == 1) {
        // REPLACE_BG: pure colour background
        fragColor = mix(u_bgColor, orig, alpha);
    } else if (u_mode == 2) {
        // TRANSPARENT: background alpha = 0
        fragColor = vec4(orig.rgb, alpha);
    } else if (u_mode == 3) {
        // IMAGE_BG: texture background
        vec4 bgPixel = texture(texBgImage, v_texCoord);
        fragColor = mix(bgPixel, orig, alpha);
    } else {
        // BLUR_BG (mode 0): darken background as placeholder (real blur = 2-pass)
        fragColor = mix(vec4(orig.rgb * 0.15, orig.a), orig, alpha);
    }
}
)";

// ---------------------------------------------------------------------------
SegmentationFilter::SegmentationFilter(
    std::shared_ptr<ai::TfliteInferenceEngine> engine,
    FrameBufferPool* pool)
    : m_engine(std::move(engine))
    , m_pool(pool)
{
    m_parameters["mode"]        = 0;
    m_parameters["blurStrength"]= 10.0f;
    m_parameters["bgColor"]     = 0xFF000000u;
    m_parameters["edgeSoften"]  = 0.5f;
}

SegmentationFilter::~SegmentationFilter() = default;

// ---------------------------------------------------------------------------
Result SegmentationFilter::initialize() {
    Result res = Filter::initialize();
    if (!res.isOk()) return res;
    cacheUniformLocations();
    return Result::ok();
}

void SegmentationFilter::onProgramRecompiled() {
    cacheUniformLocations();
}

void SegmentationFilter::cacheUniformLocations() {
    m_locInputTex   = glGetUniformLocation(m_programId, "texInput");
    m_locMaskTex    = glGetUniformLocation(m_programId, "texMask");
    m_locMode       = glGetUniformLocation(m_programId, "u_mode");
    m_locBgColor    = glGetUniformLocation(m_programId, "u_bgColor");
    m_locEdgeSoften = glGetUniformLocation(m_programId, "u_edgeSoften");
    m_locBgImageTex = glGetUniformLocation(m_programId, "texBgImage");
}

// ---------------------------------------------------------------------------
// processFrame(): 推理 → 合成
// ---------------------------------------------------------------------------
ResultPayload<Texture> SegmentationFilter::processFrame(
    const Texture& inputTexture, FrameBufferPtr outputFb)
{
    if (m_programId == 0)
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE,
            "SegmentationFilter: not initialized");
    if (!outputFb)
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE,
            "SegmentationFilter: null outputFb");
    if (!m_engine)
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE,
            "SegmentationFilter: no inference engine");
    if (!m_engine->isLoaded()) {
        LOGW("SegmentationFilter: inference engine not loaded — passing through");
        // 直通原图（不崩溃）
        onDraw(inputTexture, outputFb);
        return ResultPayload<Texture>::ok(outputFb->getTexture());
    }

    // ---- 1. 读取原图像素（用于 TFLite 推理）----
    // 我们从 inputTexture 读取 RGBA 像素（glReadPixels via FBO）
    // 注意：此操作在 GPU-CPU 之间同步，是性能瓶颈。
    // 优化方向：直接在 GPU 上 resize，但需要 PBO 或 compute shader。
    const int W = static_cast<int>(inputTexture.width);
    const int H = static_cast<int>(inputTexture.height);

    // Read-back via temporary FBO
    GLuint tmpFbo = 0;
    glGenFramebuffers(1, &tmpFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, tmpFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, inputTexture.id, 0);

    std::vector<uint8_t> pixels(static_cast<size_t>(W * H * 4));
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &tmpFbo);

    // ---- 2. 推理 ----
    auto inferResult = m_engine->runInference(pixels.data(), W, H);
    if (!inferResult.success) {
        LOGW("SegmentationFilter: inference failed: %s", inferResult.errorMessage.c_str());
        onDraw(inputTexture, outputFb);
        return ResultPayload<Texture>::ok(outputFb->getTexture());
    }
    m_lastMaskTexId = inferResult.maskTextureId;

    // ---- 3. 合成 pass ----
    onDraw(inputTexture, outputFb);
    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

// ---------------------------------------------------------------------------
// onDraw(): 绑定 uniforms → 绘制全屏四边形
// ---------------------------------------------------------------------------
void SegmentationFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();

    GLStateManager::getInstance().useProgram(m_programId);

    // texInput — slot 0
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_locInputTex, 0);

    // texMask — slot 1
    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_lastMaskTexId);
    glUniform1i(m_locMaskTex, 1);

    // texBgImage — slot 2 (IMAGE_BG mode; bind 0 when unused)
    GLStateManager::getInstance().activeTexture(GL_TEXTURE2);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D,
        m_bgImageTexId ? m_bgImageTexId : 0);
    glUniform1i(m_locBgImageTex, 2);

    // mode
    int mode = 0;
    if (m_parameters.count("mode"))
        mode = std::any_cast<int>(m_parameters.at("mode"));
    glUniform1i(m_locMode, mode);

    // bgColor (ARGB uint → normalized RGBA float)
    if (m_parameters.count("bgColor")) {
        uint32_t argb = std::any_cast<uint32_t>(m_parameters.at("bgColor"));
        float r = ((argb >> 16) & 0xFF) / 255.0f;
        float g = ((argb >>  8) & 0xFF) / 255.0f;
        float b = ((argb >>  0) & 0xFF) / 255.0f;
        float a = ((argb >> 24) & 0xFF) / 255.0f;
        glUniform4f(m_locBgColor, r, g, b, a);
    }

    // edgeSoften
    float edgeSoften = 0.5f;
    if (m_parameters.count("edgeSoften"))
        edgeSoften = std::any_cast<float>(m_parameters.at("edgeSoften"));
    glUniform1f(m_locEdgeSoften, edgeSoften);

    // 全屏四边形（通过 render device command buffer，同 GaussianBlurFilter 模式）
    if (m_renderDevice && m_quadVao) {
        auto cmd = m_renderDevice->createCommandBuffer();
        cmd->bindVertexArray(m_quadVao.get());
        cmd->draw(4);
    } else {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    outputFb->unbind();
}

std::string SegmentationFilter::getVertexShaderSource()   const { return kSegVertSrc; }
std::string SegmentationFilter::getFragmentShaderSource() const { return kSegFragSrc; }

} // namespace video
} // namespace sdk
