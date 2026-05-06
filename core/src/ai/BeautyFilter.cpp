/**
 * BeautyFilter.cpp
 *
 * 磨皮 + 美白单 pass 滤镜。
 * 皮肤检测基于 YCbCr 色彩范围，磨皮采用近似双边模糊（3×3），美白用亮度曲线。
 */

#include "../../include/ai/BeautyFilter.h"
#include "../../include/GLStateManager.h"
#include "../../include/rhi/IRenderDevice.h"

#define LOG_TAG "BeautyFilter"
#include "../../include/Log.h"

namespace sdk {
namespace video {

// ---------------------------------------------------------------------------
// 内联 fragment shader（与 assets/shaders/beauty.frag 保持一致）
// ---------------------------------------------------------------------------
static const char* kBeautyFragSrc = R"(#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D inputImageTexture;
uniform float     u_smoothStrength;
uniform float     u_whitenStrength;
uniform vec2      u_texelSize;
out vec4 fragColor;

vec3 rgbToYCbCr(vec3 rgb) {
    float y  =  0.299*rgb.r + 0.587*rgb.g + 0.114*rgb.b;
    float cb = -0.169*rgb.r - 0.331*rgb.g + 0.500*rgb.b + 0.5;
    float cr =  0.500*rgb.r - 0.419*rgb.g - 0.081*rgb.b + 0.5;
    return vec3(y, cb, cr);
}
float skinMask(vec3 rgb) {
    vec3  ycbcr = rgbToYCbCr(rgb);
    float y  = ycbcr.x;
    float cb = ycbcr.y * 255.0;
    float cr = ycbcr.z * 255.0;
    return step(0.1,y)*step(y,0.95)*step(77.0,cb)*step(cb,127.0)*step(133.0,cr)*step(cr,173.0);
}
vec3 bilateralSmooth(vec2 uv, vec3 center) {
    vec3 sum = vec3(0.0); float wSum = 0.0;
    float sigmaC = 0.12;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec3  n = texture(inputImageTexture, uv + vec2(float(dx),float(dy))*u_texelSize).rgb;
            float dc = length(n - center);
            float wc = exp(-dc*dc/(2.0*sigmaC*sigmaC));
            float ws = exp(-float(dx*dx+dy*dy)/2.0);
            float w  = wc * ws;
            sum += n * w; wSum += w;
        }
    }
    return sum / wSum;
}
vec3 whitenCurve(vec3 rgb, float s) {
    float gamma = 1.0 - s * 0.4;
    return mix(rgb, pow(rgb, vec3(gamma)), s * 0.8);
}
void main() {
    vec4 orig   = texture(inputImageTexture, v_texCoord);
    vec3 color  = orig.rgb;
    float skin  = skinMask(color);
    vec3 smooth = bilateralSmooth(v_texCoord, color);
    vec3 result = mix(color, smooth, skin * u_smoothStrength);
    result      = mix(result, whitenCurve(result, u_whitenStrength), skin);
    fragColor   = vec4(result, orig.a);
}
)";

// ---------------------------------------------------------------------------
BeautyFilter::BeautyFilter() {
    m_parameters["smoothStrength"] = 0.6f;
    m_parameters["whitenStrength"] = 0.4f;
}

Result BeautyFilter::initialize() {
    Result res = Filter::initialize();
    if (!res.isOk()) return res;
    cacheUniformLocations();
    return Result::ok();
}

void BeautyFilter::onProgramRecompiled() {
    cacheUniformLocations();
}

void BeautyFilter::cacheUniformLocations() {
    m_locSmoothStrength = glGetUniformLocation(m_programId, "u_smoothStrength");
    m_locWhitenStrength = glGetUniformLocation(m_programId, "u_whitenStrength");
    m_locTexelSize      = glGetUniformLocation(m_programId, "u_texelSize");
}

// ---------------------------------------------------------------------------
void BeautyFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    // inputImageTexture — slot 0
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    // smoothStrength
    float smooth = 0.6f;
    if (m_parameters.count("smoothStrength"))
        smooth = std::any_cast<float>(m_parameters.at("smoothStrength"));
    glUniform1f(m_locSmoothStrength, smooth);

    // whitenStrength
    float whiten = 0.4f;
    if (m_parameters.count("whitenStrength"))
        whiten = std::any_cast<float>(m_parameters.at("whitenStrength"));
    glUniform1f(m_locWhitenStrength, whiten);

    // texelSize
    float tw = (inputTexture.width  > 0) ? 1.0f / static_cast<float>(inputTexture.width)  : 0.001f;
    float th = (inputTexture.height > 0) ? 1.0f / static_cast<float>(inputTexture.height) : 0.001f;
    glUniform2f(m_locTexelSize, tw, th);

    // 全屏四边形
    if (m_renderDevice && m_quadVao) {
        auto cmd = m_renderDevice->createCommandBuffer();
        cmd->bindVertexArray(m_quadVao.get());
        cmd->draw(4);
    } else {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    outputFb->unbind();
}

std::string BeautyFilter::getFragmentShaderSource() const { return kBeautyFragSrc; }

} // namespace video
} // namespace sdk
