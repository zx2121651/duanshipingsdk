/**
 * ChromaKeyFilter.cpp
 *
 * HSV 色度键抠图滤镜实现。
 */

#include "../../include/ai/ChromaKeyFilter.h"
#include "../../include/GLStateManager.h"

#define LOG_TAG "ChromaKeyFilter"
#include "../../include/Log.h"

#include <cmath>
#include <algorithm>

namespace sdk {
namespace video {

// ---------------------------------------------------------------------------
// Vertex shader (passthrough)
// ---------------------------------------------------------------------------
static const char* kChromaVertSrc = R"(#version 300 es
in  vec4 position;
in  vec2 texCoord;
out vec2 v_uv;
void main() {
    gl_Position = position;
    v_uv        = texCoord;
}
)";

// ---------------------------------------------------------------------------
// Fragment shader — HSV chroma key
// ---------------------------------------------------------------------------
static const char* kChromaFragSrc = R"(#version 300 es
precision highp float;
in  vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_input;
uniform sampler2D u_bgImage;
uniform float     u_hueCenter;   // [0,1]
uniform float     u_hueTol;      // single-side tolerance [0,0.5]
uniform float     u_satMin;      // saturation floor
uniform float     u_edgeSoft;    // softness radius
uniform float     u_spill;       // spill suppression [0,1]
uniform vec4      u_bgColor;     // for REPLACE_BG mode (RGBA normalized)
uniform int       u_mode;        // 0=TRANSPARENT, 1=REPLACE_BG, 2=IMAGE_BG

// ── RGB → HSV ─────────────────────────────────────────────────────────────
vec3 rgb2hsv(vec3 c) {
    float cmax = max(c.r, max(c.g, c.b));
    float cmin = min(c.r, min(c.g, c.b));
    float delta = cmax - cmin;
    float h = 0.0;
    if (delta > 0.0001) {
        if (cmax == c.r)      h = mod((c.g - c.b) / delta, 6.0) / 6.0;
        else if (cmax == c.g) h = ((c.b - c.r) / delta + 2.0) / 6.0;
        else                  h = ((c.r - c.g) / delta + 4.0) / 6.0;
        if (h < 0.0) h += 1.0;
    }
    float s = (cmax < 0.0001) ? 0.0 : delta / cmax;
    return vec3(h, s, cmax);
}

void main() {
    vec4  orig = texture(u_input, v_uv);
    vec3  hsv  = rgb2hsv(orig.rgb);

    // Hue distance (wrap-around)
    float hueDist = abs(hsv.x - u_hueCenter);
    hueDist = min(hueDist, 1.0 - hueDist);  // wrap

    // Compute foreground mask:
    //   alpha=1 → fully foreground (keep)
    //   alpha=0 → fully background (remove)
    float keyStrength = 0.0;
    if (hsv.y >= u_satMin) {
        float softLo = u_hueTol - u_edgeSoft * 0.5;
        float softHi = u_hueTol + u_edgeSoft * 0.5;
        keyStrength = 1.0 - smoothstep(softLo, softHi, hueDist);
    }
    float fgAlpha = 1.0 - keyStrength;

    // Spill suppression: reduce green channel in edge region
    vec3  fgRgb = orig.rgb;
    if (u_spill > 0.0 && keyStrength > 0.0 && keyStrength < 1.0) {
        float desat = mix(0.0, u_spill, keyStrength);
        float luma  = dot(fgRgb, vec3(0.299, 0.587, 0.114));
        fgRgb = mix(fgRgb, vec3(luma), desat);
    }

    // Compose output
    vec4 bgPixel;
    if (u_mode == 1) {
        bgPixel = u_bgColor;
    } else if (u_mode == 2) {
        bgPixel = texture(u_bgImage, v_uv);
    } else {
        bgPixel = vec4(0.0);  // TRANSPARENT
    }

    fragColor = mix(bgPixel, vec4(fgRgb, orig.a), fgAlpha);
    if (u_mode == 0) {
        fragColor.a = fgAlpha;
    }
}
)";

// ---------------------------------------------------------------------------
ChromaKeyFilter::ChromaKeyFilter(FrameBufferPool* pool)
    : m_pool(pool)
{}

ChromaKeyFilter::~ChromaKeyFilter() = default;

// ---------------------------------------------------------------------------
Result ChromaKeyFilter::initialize() {
    Result r = Filter::initialize();
    if (!r.isOk()) return r;
    cacheUniformLocations();
    return Result::ok();
}

void ChromaKeyFilter::onProgramRecompiled() {
    cacheUniformLocations();
}

void ChromaKeyFilter::cacheUniformLocations() {
    m_locInput     = glGetUniformLocation(m_programId, "u_input");
    m_locBgImage   = glGetUniformLocation(m_programId, "u_bgImage");
    m_locHueCenter = glGetUniformLocation(m_programId, "u_hueCenter");
    m_locHueTol    = glGetUniformLocation(m_programId, "u_hueTol");
    m_locSatMin    = glGetUniformLocation(m_programId, "u_satMin");
    m_locEdgeSoft  = glGetUniformLocation(m_programId, "u_edgeSoft");
    m_locSpill     = glGetUniformLocation(m_programId, "u_spill");
    m_locBgColor   = glGetUniformLocation(m_programId, "u_bgColor");
    m_locMode      = glGetUniformLocation(m_programId, "u_mode");
}

std::string ChromaKeyFilter::getVertexShaderSource() const {
    return kChromaVertSrc;
}

std::string ChromaKeyFilter::getFragmentShaderSource() const {
    return kChromaFragSrc;
}

// ---------------------------------------------------------------------------
// setKeyColor / setKeyColorFromARGB
// ---------------------------------------------------------------------------
void ChromaKeyFilter::setKeyColor(float hueCenter, float hueTol, float satMin) {
    m_hueCenter = hueCenter;
    m_hueTol    = hueTol;
    m_satMin    = satMin;
}

void ChromaKeyFilter::rgbToHsv(float r, float g, float b,
                                 float& h, float& s, float& v) {
    float cmax = std::max({r, g, b});
    float cmin = std::min({r, g, b});
    float delta = cmax - cmin;
    v = cmax;
    s = (cmax < 1e-4f) ? 0.f : delta / cmax;
    if (delta < 1e-4f) { h = 0.f; return; }
    if (cmax == r)      h = std::fmod((g - b) / delta, 6.f) / 6.f;
    else if (cmax == g) h = ((b - r) / delta + 2.f) / 6.f;
    else                h = ((r - g) / delta + 4.f) / 6.f;
    if (h < 0.f) h += 1.f;
}

void ChromaKeyFilter::setKeyColorFromARGB(uint32_t argb) {
    float r = ((argb >> 16) & 0xFF) / 255.f;
    float g = ((argb >>  8) & 0xFF) / 255.f;
    float b = ( argb        & 0xFF) / 255.f;
    float h, s, v;
    rgbToHsv(r, g, b, h, s, v);
    m_hueCenter = h;
    // keep existing tolerance/satMin unless caller sets them explicitly
}

// ---------------------------------------------------------------------------
// processFrame
// ---------------------------------------------------------------------------
ResultPayload<Texture> ChromaKeyFilter::processFrame(
    const Texture& inputTexture, FrameBufferPtr outputFb)
{
    if (m_programId == 0)
        return ResultPayload<Texture>::error(ERR_RENDER_INVALID_STATE,
            "ChromaKeyFilter: not initialized");
    if (!outputFb)
        return ResultPayload<Texture>::error(ERR_RENDER_INVALID_STATE,
            "ChromaKeyFilter: null outputFb");
    onDraw(inputTexture, outputFb);
    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

void ChromaKeyFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();

    GLStateManager::getInstance().useProgram(m_programId);

    // textures
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_locInput, 0);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D,
        m_bgImageTexId ? m_bgImageTexId : 0);
    glUniform1i(m_locBgImage, 1);

    // chroma params
    glUniform1f(m_locHueCenter, m_hueCenter);
    glUniform1f(m_locHueTol,    m_hueTol);
    glUniform1f(m_locSatMin,    m_satMin);
    glUniform1f(m_locEdgeSoft,  m_edgeSoftness);
    glUniform1f(m_locSpill,     m_spillSuppression);
    glUniform1i(m_locMode,      static_cast<int>(m_mode));

    // bgColor (ARGB → normalized RGBA float)
    float r = ((m_bgColor >> 16) & 0xFF) / 255.f;
    float g = ((m_bgColor >>  8) & 0xFF) / 255.f;
    float b = ( m_bgColor        & 0xFF) / 255.f;
    float a = ((m_bgColor >> 24) & 0xFF) / 255.f;
    glUniform4f(m_locBgColor, r, g, b, a);

    if (m_renderDevice && m_quadVao) {
        auto cmd = m_renderDevice->createCommandBuffer();
        cmd->bindVertexArray(m_quadVao.get());
        cmd->draw(4);
    } else {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    outputFb->unbind();
}

} // namespace video
} // namespace sdk
