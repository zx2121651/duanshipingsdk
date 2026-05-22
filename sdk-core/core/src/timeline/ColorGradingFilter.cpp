/**
 * ColorGradingFilter.cpp
 *
 * 色彩分级滤镜实现。GLSL 单 pass：基础调色 → 曲线查表 → HSL → 色轮 → LUT3D
 */

#include "../../include/timeline/ColorGradingFilter.h"
#include "../../include/GLStateManager.h"
#include "../../include/rhi/IRenderDevice.h"

#define LOG_TAG "ColorGradingFilter"
#include "../../include/Log.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// GLSL fragment shader source
// ---------------------------------------------------------------------------
static const char* kCGFragSrc = R"(#version 300 es
precision highp float;

in vec2 v_texCoord;
uniform sampler2D u_inputTex;
uniform sampler2D u_curveTex;    // 1D curve LUT  (256×1 RGBA: R/G/B/master)
uniform sampler3D u_lut3d;       // 3D LUT
uniform int       u_useLut;      // 0=no lut

uniform float u_brightness;
uniform float u_contrast;
uniform float u_saturation;
uniform float u_temperature;
uniform float u_tint;
uniform float u_exposure;
uniform float u_highlights;
uniform float u_shadows;

// Color wheel
uniform vec3 u_shadowLift;
uniform vec3 u_midBalance;
uniform vec3 u_highlightGain;

out vec4 fragColor;

// --- helpers ---
vec3 rgb2hsl(vec3 c) {
    float hi = max(max(c.r,c.g),c.b);
    float lo = min(min(c.r,c.g),c.b);
    float d  = hi - lo;
    float h  = 0.0;
    if (d > 0.001) {
        if (hi == c.r)      h = mod((c.g-c.b)/d, 6.0);
        else if (hi == c.g) h = (c.b-c.r)/d + 2.0;
        else                h = (c.r-c.g)/d + 4.0;
        h /= 6.0;
    }
    float l = (hi+lo)*0.5;
    float s = (d < 0.001) ? 0.0 : d / (1.0 - abs(2.0*l - 1.0));
    return vec3(h, clamp(s,0.,1.), clamp(l,0.,1.));
}

vec3 hsl2rgb(vec3 hsl) {
    float h=hsl.x, s=hsl.y, l=hsl.z;
    float c = (1.0-abs(2.0*l-1.0))*s;
    float x = c*(1.0-abs(mod(h*6.0,2.0)-1.0));
    float m = l - c*0.5;
    vec3 rgb;
    float hh = h*6.0;
    if      (hh<1.) rgb=vec3(c,x,0.);
    else if (hh<2.) rgb=vec3(x,c,0.);
    else if (hh<3.) rgb=vec3(0.,c,x);
    else if (hh<4.) rgb=vec3(0.,x,c);
    else if (hh<5.) rgb=vec3(x,0.,c);
    else            rgb=vec3(c,0.,x);
    return clamp(rgb+vec3(m),0.,1.);
}

void main() {
    vec4 src = texture(u_inputTex, v_texCoord);
    vec3 col = src.rgb;

    // Exposure
    col *= pow(2.0, u_exposure);

    // Brightness / Contrast
    col = col * u_contrast + u_brightness;

    // Highlights / Shadows
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col += u_highlights * smoothstep(0.5, 1.0, lum);
    col += u_shadows    * (1.0 - smoothstep(0.0, 0.5, lum));

    // Temperature (blue-yellow axis) + Tint (green-magenta axis)
    col.r += u_temperature * 0.1;
    col.b -= u_temperature * 0.1;
    col.g += u_tint * 0.1;

    // Saturation
    float grey = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(grey), col, u_saturation);

    col = clamp(col, 0.0, 1.0);

    // RGB curve lookup
    float rr = texture(u_curveTex, vec2(col.r, 0.5)).r;
    float gg = texture(u_curveTex, vec2(col.g, 0.5)).g;
    float bb = texture(u_curveTex, vec2(col.b, 0.5)).b;
    float mm = texture(u_curveTex, vec2((col.r+col.g+col.b)/3.0, 0.5)).a;
    col = clamp(vec3(rr,gg,bb) * mm * 3.0, 0.0, 1.0);

    // Color wheel: shadow / midtone / highlight
    float shadow_w    = 1.0 - smoothstep(0.0, 0.4, lum);
    float highlight_w = smoothstep(0.6, 1.0, lum);
    float mid_w       = 1.0 - shadow_w - highlight_w;
    col += u_shadowLift    * shadow_w;
    col += u_midBalance    * mid_w;
    col  = col * mix(vec3(1.0), u_highlightGain, highlight_w);
    col  = clamp(col, 0.0, 1.0);

    // 3D LUT
    if (u_useLut != 0) {
        col = texture(u_lut3d, col).rgb;
    }

    fragColor = vec4(col, src.a);
}
)";

static const char* kCGVertSrc = R"(#version 300 es
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texCoord;
out vec2 v_texCoord;
void main() {
    gl_Position = position;
    v_texCoord  = texCoord;
}
)";

// ---------------------------------------------------------------------------
ColorGradingFilter::ColorGradingFilter() = default;
ColorGradingFilter::~ColorGradingFilter() {
    if (m_ownLut && m_lutTexId) glDeleteTextures(1, &m_lutTexId);
    if (m_curveTexId) glDeleteTextures(1, &m_curveTexId);
}

Result ColorGradingFilter::initialize() {
    Result r = Filter::initialize();
    if (!r.isOk()) return r;
    cacheUniformLocations();
    uploadCurveTexture();
    if (!m_lutData.empty()) uploadLutTexture();
    return Result::ok();
}

void ColorGradingFilter::onProgramRecompiled() {
    cacheUniformLocations();
}

void ColorGradingFilter::cacheUniformLocations() {
    m_locInputTex   = glGetUniformLocation(m_programId, "u_inputTex");
    m_locLutTex     = glGetUniformLocation(m_programId, "u_lut3d");
    m_locCurveTex   = glGetUniformLocation(m_programId, "u_curveTex");
    m_locBrightness = glGetUniformLocation(m_programId, "u_brightness");
    m_locContrast   = glGetUniformLocation(m_programId, "u_contrast");
    m_locSaturation = glGetUniformLocation(m_programId, "u_saturation");
    m_locTemperature= glGetUniformLocation(m_programId, "u_temperature");
    m_locTint       = glGetUniformLocation(m_programId, "u_tint");
    m_locExposure   = glGetUniformLocation(m_programId, "u_exposure");
    m_locHighlights = glGetUniformLocation(m_programId, "u_highlights");
    m_locShadows    = glGetUniformLocation(m_programId, "u_shadows");
    m_locUseLut     = glGetUniformLocation(m_programId, "u_useLut");
}

// ---------------------------------------------------------------------------
// uploadCurveTexture — 256×1 RGBA texture encoding R/G/B/master curves
// ---------------------------------------------------------------------------
void ColorGradingFilter::uploadCurveTexture() {
    if (m_curveTexId == 0) glGenTextures(1, &m_curveTexId);
    std::array<uint8_t, 256*4> data;
    for (int i = 0; i < 256; ++i) {
        data[i*4+0] = m_curve.r[i];
        data[i*4+1] = m_curve.g[i];
        data[i*4+2] = m_curve.b[i];
        data[i*4+3] = m_curve.master[i];
    }
    glBindTexture(GL_TEXTURE_2D, m_curveTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// ---------------------------------------------------------------------------
// uploadLutTexture — 3D LUT texture from m_lutData
// ---------------------------------------------------------------------------
void ColorGradingFilter::uploadLutTexture() {
    if (m_lutData.empty()) return;
    if (m_ownLut && m_lutTexId) glDeleteTextures(1, &m_lutTexId);
    glGenTextures(1, &m_lutTexId);
    m_ownLut = true;
    glBindTexture(GL_TEXTURE_3D, m_lutTexId);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, m_lutSize, m_lutSize, m_lutSize,
                 0, GL_RGB, GL_UNSIGNED_BYTE, m_lutData.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    m_lutData.clear();
    LOGI("ColorGradingFilter: 3D LUT uploaded (%dx%dx%d)", m_lutSize, m_lutSize, m_lutSize);
}

// ---------------------------------------------------------------------------
// parseCubeFile — simple .cube parser
// ---------------------------------------------------------------------------
bool ColorGradingFilter::parseCubeFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) { LOGE("ColorGradingFilter: cannot open %s", path.c_str()); return false; }
    m_lutSize = 32;
    std::vector<float> vals;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.substr(0, 10) == "LUT_3D_SIZE") {
            std::istringstream ss(line.substr(10));
            ss >> m_lutSize;
            continue;
        }
        std::istringstream ss(line);
        float r,g,b;
        if (ss >> r >> g >> b) { vals.push_back(r); vals.push_back(g); vals.push_back(b); }
    }
    int expected = m_lutSize * m_lutSize * m_lutSize * 3;
    if ((int)vals.size() < expected) {
        LOGE("ColorGradingFilter: LUT data too short (%zu < %d)", vals.size(), expected);
        return false;
    }
    m_lutData.resize(expected);
    for (int i = 0; i < expected; ++i)
        m_lutData[i] = (uint8_t)(std::min(std::max(vals[i], 0.f), 1.f) * 255.f);
    return true;
}

bool ColorGradingFilter::loadLutFromCubeFile(const std::string& path) {
    return parseCubeFile(path);
    // GPU upload deferred to next initialize() or processFrame()
}

void ColorGradingFilter::setLutTexture(GLuint texId, int lutSize) {
    if (m_ownLut && m_lutTexId) glDeleteTextures(1, &m_lutTexId);
    m_lutTexId = texId;
    m_lutSize  = lutSize;
    m_ownLut   = false;
}

void ColorGradingFilter::clearLut() {
    if (m_ownLut && m_lutTexId) glDeleteTextures(1, &m_lutTexId);
    m_lutTexId = 0; m_ownLut = false; m_lutData.clear();
}

void ColorGradingFilter::setCurves(const RGBCurve& curve) {
    m_curve = curve;
    if (m_curveTexId) uploadCurveTexture();
}

void ColorGradingFilter::resetCurves() {
    m_curve = RGBCurve();
    if (m_curveTexId) uploadCurveTexture();
}

void ColorGradingFilter::resetAll() {
    m_brightness=0.f; m_contrast=1.f; m_saturation=1.f;
    m_temperature=0.f; m_tint=0.f; m_exposure=0.f;
    m_highlights=0.f; m_shadows=0.f;
    m_curve = RGBCurve();
    m_hsl   = HSLAdjustment();
    m_wheels= ColorWheels();
    clearLut();
    if (m_curveTexId) uploadCurveTexture();
}

// ---------------------------------------------------------------------------
// onDraw
// ---------------------------------------------------------------------------
void ColorGradingFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    // Upload pending LUT if any
    if (!m_lutData.empty() && m_programId != 0) uploadLutTexture();

    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    // Input
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_locInputTex, 0);

    // Curve tex
    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_curveTexId);
    glUniform1i(m_locCurveTex, 1);

    // LUT
    glUniform1i(m_locUseLut, m_lutTexId ? 1 : 0);
    if (m_lutTexId) {
        GLStateManager::getInstance().activeTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_3D, m_lutTexId);
        glUniform1i(m_locLutTex, 2);
    }

    // Basic uniforms
    glUniform1f(m_locBrightness,  m_brightness);
    glUniform1f(m_locContrast,    m_contrast);
    glUniform1f(m_locSaturation,  m_saturation);
    glUniform1f(m_locTemperature, m_temperature);
    glUniform1f(m_locTint,        m_tint);
    glUniform1f(m_locExposure,    m_exposure);
    glUniform1f(m_locHighlights,  m_highlights);
    glUniform1f(m_locShadows,     m_shadows);

    // Color wheel uniforms
    auto locSL = glGetUniformLocation(m_programId, "u_shadowLift");
    auto locMB = glGetUniformLocation(m_programId, "u_midBalance");
    auto locHG = glGetUniformLocation(m_programId, "u_highlightGain");
    glUniform3fv(locSL, 1, m_wheels.shadowLift);
    glUniform3fv(locMB, 1, m_wheels.midtoneBalance);
    glUniform3fv(locHG, 1, m_wheels.highlightGain);

    if (m_renderDevice && m_quadVao) {
        auto cmd = m_renderDevice->createCommandBuffer();
        cmd->bindVertexArray(m_quadVao.get());
        cmd->draw(4);
    } else {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    outputFb->unbind();
}

std::string ColorGradingFilter::getFragmentShaderSource() const { return kCGFragSrc; }
std::string ColorGradingFilter::getVertexShaderSource()   const { return kCGVertSrc; }

} // namespace timeline
} // namespace video
} // namespace sdk
