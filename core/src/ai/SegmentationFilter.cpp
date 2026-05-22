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
        // BG_COLOR: pure colour background
        fragColor = mix(u_bgColor, orig, alpha);
    } else if (u_mode == 2) {
        // TRANSPARENT: background alpha = 0
        fragColor = vec4(orig.rgb, alpha);
    } else if (u_mode == 3 || u_mode == 0) {
        // BG_IMAGE or BLUR: texture background
        vec4 bgPixel = texture(texBgImage, v_texCoord);
        fragColor = mix(bgPixel, orig, alpha);
    } else if (u_mode == 4) {
        // ORIGINAL: bypass
        fragColor = orig;
    } else {
        fragColor = orig;
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
    m_parameters["mode"]         = 0;
    m_parameters["blurStrength"] = 10.0f;
    m_parameters["bgColor"]      = 0xFF000000u;
    m_parameters["edgeSoften"]   = 0.5f;
    m_parameters["bgImageTexture"] = 0u;
}

SegmentationFilter::~SegmentationFilter() {
    if (m_pool && m_blurredFb) {
        m_pool->release(m_blurredFb);
    }
}

void SegmentationFilter::setBgImageTexture(GLuint texId) {
    m_bgImageTexId = texId;
    m_parameters["bgImageTexture"] = texId;
}

// ---------------------------------------------------------------------------
Result SegmentationFilter::initialize() {
    Result res = Filter::initialize();
    if (!res.isOk()) return res;
    cacheUniformLocations();

    // 验证 Shader Uniform 位置
    if (m_locInputTex == (GLuint)-1) LOGW("SegmentationFilter: uniform 'texInput' not found");
    if (m_locMaskTex == (GLuint)-1) LOGW("SegmentationFilter: uniform 'texMask' not found");
    if (m_locMode == (GLuint)-1) LOGW("SegmentationFilter: uniform 'u_mode' not found");
    if (m_locBgColor == (GLuint)-1) LOGW("SegmentationFilter: uniform 'u_bgColor' not found");
    if (m_locEdgeSoften == (GLuint)-1) LOGW("SegmentationFilter: uniform 'u_edgeSoften' not found");
    if (m_locBgImageTex == (GLuint)-1) LOGW("SegmentationFilter: uniform 'texBgImage' not found");

    return Result::ok();
}

void SegmentationFilter::onProgramRecompiled() {
    cacheUniformLocations();
}

void SegmentationFilter::setParameter(const std::string& key, const std::any& value) {
    if (key == "bgImageTexture") {
        try {
            m_bgImageTexId = std::any_cast<uint32_t>(value);
        } catch (...) {
            try {
                m_bgImageTexId = static_cast<uint32_t>(std::any_cast<int>(value));
            } catch (...) {}
        }
    }
    Filter::setParameter(key, value);
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

    // ---- 1. 背景预处理 (BLUR) ----
    Mode mode = getMode();
    if (mode == Mode::BLUR && m_pool) {
        if (!m_blurFilter) {
            m_blurFilter = std::make_unique<GaussianBlurFilter>(m_pool);
            m_blurFilter->setRenderDevice(m_renderDevice);
            m_blurFilter->setShaderManager(m_shaderManager);
            m_blurFilter->setQuadVao(m_quadVao);
            m_blurFilter->initialize();
        }
        m_blurFilter->setParameter("blurSize", getBlurStrength());
        if (m_blurredFb) m_pool->release(m_blurredFb);
        m_blurredFb = m_pool->get(inputTexture.width, inputTexture.height);
        if (m_blurredFb) {
            m_blurFilter->processFrame(inputTexture, m_blurredFb);
        }
    }

    if (!m_engine || !m_engine->isLoaded()) {
        LOGW("SegmentationFilter: inference engine not loaded — passing through");
        // 直通原图（不崩溃）
        onDraw(inputTexture, outputFb);
        return ResultPayload<Texture>::ok(outputFb->getTexture());
    }

    // ---- 2. GPU 下采样与 TFLite 推理 ----
    const int W = static_cast<int>(inputTexture.width);
    const int H = static_cast<int>(inputTexture.height);

    auto inferResult = m_engine->runInference(inputTexture.id, W, H);
    if (!inferResult.success) {
        LOGW("SegmentationFilter: inference failed: %s", inferResult.errorMessage.c_str());
        onDraw(inputTexture, outputFb);
        return ResultPayload<Texture>::ok(outputFb->getTexture());
    }
    m_lastMaskTexId = inferResult.maskTextureId;

    // ---- 4. 合成 pass ----
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

    // texBgImage — slot 2 (BG_IMAGE or BLUR mode)
    Mode mode = getMode();
    GLuint bgTexId = 0;
    if (mode == Mode::BG_IMAGE) {
        bgTexId = m_bgImageTexId;
    } else if (mode == Mode::BLUR && m_blurredFb) {
        bgTexId = m_blurredFb->getTexture().id;
    }

    GLStateManager::getInstance().activeTexture(GL_TEXTURE2);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, bgTexId);
    glUniform1i(m_locBgImageTex, 2);

    // mode
    glUniform1i(m_locMode, static_cast<int>(mode));

    // bgColor (ARGB uint → normalized RGBA float)
    uint32_t argb = getBgColor();
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >>  8) & 0xFF) / 255.0f;
    float b = ((argb >>  0) & 0xFF) / 255.0f;
    float a = ((argb >> 24) & 0xFF) / 255.0f;
    glUniform4f(m_locBgColor, r, g, b, a);

    // edgeSoften
    glUniform1f(m_locEdgeSoften, getEdgeSoften());

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

SegmentationFilter::Mode SegmentationFilter::getMode() const {
    if (m_parameters.count("mode")) {
        try {
            return static_cast<Mode>(std::any_cast<int>(m_parameters.at("mode")));
        } catch (...) {}
    }
    return Mode::BLUR;
}

float SegmentationFilter::getBlurStrength() const {
    if (m_parameters.count("blurStrength")) {
        try {
            return std::any_cast<float>(m_parameters.at("blurStrength"));
        } catch (...) {}
    }
    return 10.0f;
}

uint32_t SegmentationFilter::getBgColor() const {
    if (m_parameters.count("bgColor")) {
        try {
            return std::any_cast<uint32_t>(m_parameters.at("bgColor"));
        } catch (...) {}
    }
    return 0xFF000000u;
}

float SegmentationFilter::getEdgeSoften() const {
    if (m_parameters.count("edgeSoften")) {
        try {
            return std::any_cast<float>(m_parameters.at("edgeSoften"));
        } catch (...) {}
    }
    return 0.5f;
}

} // namespace video
} // namespace sdk
