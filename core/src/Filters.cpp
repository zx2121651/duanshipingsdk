#include "../include/Filters.h"
#include <iostream>
#include <vector>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

namespace sdk {
namespace video {

// --- Shader sources ---

const char* kOESVertexShader = R"(
#version 300 es
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 inputTextureCoordinate;
uniform mat4 textureMatrix;
uniform bool flipHorizontal;
uniform bool flipVertical;
out vec2 textureCoordinate;
void main() {
    gl_Position = position;
    // Apply SurfaceTexture transform matrix to fix orientation/cropping
    vec2 coord = (textureMatrix * inputTextureCoordinate).xy;

    // Apply manual flips if necessary
    if (flipHorizontal) coord.x = 1.0 - coord.x;
    if (flipVertical) coord.y = 1.0 - coord.y;

    textureCoordinate = coord;
}
)";

const char* kOESFragmentShader = R"(
#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;
uniform samplerExternalOES inputImageTexture;

void main() {
    fragColor = texture(inputImageTexture, textureCoordinate);
}
)";

const char* kBrightnessFragmentShader = R"(
#version 300 es
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;
uniform sampler2D inputImageTexture;
uniform float brightness;

void main() {
    vec4 textureColor = texture(inputImageTexture, textureCoordinate);
    fragColor = vec4((textureColor.rgb + vec3(brightness)), textureColor.w);
}
)";

const char* kGaussianBlurFragmentShader = R"(
#version 300 es
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;
uniform sampler2D inputImageTexture;
uniform float texelWidthOffset;
uniform float texelHeightOffset;
uniform float blurSize;

// Note: For a true and efficient Gaussian Blur, this should be split into a
// two-pass pipeline (horizontal and vertical). This single-pass implementation
// is kept for simplicity as an example.
void main() {
    vec2 singleStepOffset = vec2(texelWidthOffset, texelHeightOffset) * blurSize;
    vec4 sum = vec4(0.0);

    sum += texture(inputImageTexture, textureCoordinate - singleStepOffset * 4.0) * 0.05;
    sum += texture(inputImageTexture, textureCoordinate - singleStepOffset * 3.0) * 0.09;
    sum += texture(inputImageTexture, textureCoordinate - singleStepOffset * 2.0) * 0.12;
    sum += texture(inputImageTexture, textureCoordinate - singleStepOffset * 1.0) * 0.15;
    sum += texture(inputImageTexture, textureCoordinate) * 0.18;
    sum += texture(inputImageTexture, textureCoordinate + singleStepOffset * 1.0) * 0.15;
    sum += texture(inputImageTexture, textureCoordinate + singleStepOffset * 2.0) * 0.12;
    sum += texture(inputImageTexture, textureCoordinate + singleStepOffset * 3.0) * 0.09;
    sum += texture(inputImageTexture, textureCoordinate + singleStepOffset * 4.0) * 0.05;

    fragColor = sum;
}
)";

const char* kLookupFragmentShader = R"(
#version 300 es
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;

uniform sampler2D inputImageTexture;
uniform sampler2D lookupTexture;
uniform float intensity;

void main() {
    vec4 textureColor = texture(inputImageTexture, textureCoordinate);
    float blueColor = textureColor.b * 63.0;

    vec2 quad1;
    quad1.y = floor(floor(blueColor) / 8.0);
    quad1.x = floor(blueColor) - (quad1.y * 8.0);

    vec2 quad2;
    quad2.y = floor(ceil(blueColor) / 8.0);
    quad2.x = ceil(blueColor) - (quad2.y * 8.0);

    vec2 texPos1;
    texPos1.x = (quad1.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);
    texPos1.y = (quad1.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);

    vec2 texPos2;
    texPos2.x = (quad2.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);
    texPos2.y = (quad2.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);

    vec4 newColor1 = texture(lookupTexture, texPos1);
    vec4 newColor2 = texture(lookupTexture, texPos2);

    vec4 newColor = mix(newColor1, newColor2, fract(blueColor));
    fragColor = mix(textureColor, vec4(newColor.rgb, textureColor.w), intensity);
}
)";

const char* kBilateralFragmentShader = R"(
#version 300 es
precision mediump float;

in vec2 textureCoordinate;
out vec4 fragColor;

uniform sampler2D inputImageTexture;

uniform float texelWidthOffset;
uniform float texelHeightOffset;
uniform float distanceNormalizationFactor; // Control smoothing strength

void main() {
    vec4 centralColor = texture(inputImageTexture, textureCoordinate);

    // Quick escape for low smoothing (acts as passthrough or just performance saver)
    if (distanceNormalizationFactor < 0.1) {
        fragColor = centralColor;
        return;
    }

    float gaussianWeightTotal = 0.15;
    vec4 sum = centralColor * 0.15;

    vec2 singleStepOffset = vec2(texelWidthOffset, texelHeightOffset);

    float distance_diff;
    vec4 sampleColor;
    float weight;

    // A simplified 3x3 bilateral filter kernel for real-time mobile performance
    // It compares the central pixel color with neighbors. Neighbors with similar colors
    // get higher weights (smoothing continuous areas like skin), while sharp edges
    // are preserved because of high color difference lowering the weight.

    // (-1, -1)
    sampleColor = texture(inputImageTexture, textureCoordinate - singleStepOffset);
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.05 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (0, -1)
    sampleColor = texture(inputImageTexture, textureCoordinate - vec2(0.0, singleStepOffset.y));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.10 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (1, -1)
    sampleColor = texture(inputImageTexture, textureCoordinate + vec2(singleStepOffset.x, -singleStepOffset.y));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.05 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (-1, 0)
    sampleColor = texture(inputImageTexture, textureCoordinate - vec2(singleStepOffset.x, 0.0));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.10 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (1, 0)
    sampleColor = texture(inputImageTexture, textureCoordinate + vec2(singleStepOffset.x, 0.0));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.10 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (-1, 1)
    sampleColor = texture(inputImageTexture, textureCoordinate + vec2(-singleStepOffset.x, singleStepOffset.y));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.05 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (0, 1)
    sampleColor = texture(inputImageTexture, textureCoordinate + vec2(0.0, singleStepOffset.y));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.10 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (1, 1)
    sampleColor = texture(inputImageTexture, textureCoordinate + singleStepOffset);
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.05 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    fragColor = vec4(sum.rgb / gaussianWeightTotal, centralColor.a);
}
)";

// Common vertex coordinates
static const float s_vertexCoords[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
};

static const float s_textureCoords[] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
};

// --- OES2RGBFilter ---

OES2RGBFilter::OES2RGBFilter() {
    // Default to identity matrix
    m_parameters["textureMatrix"] = std::vector<float>{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    m_parameters["flipHorizontal"] = false;
    m_parameters["flipVertical"] = false;
}

void OES2RGBFilter::initialize() {
    Filter::initialize();
    m_textureMatrixHandle = glGetUniformLocation(m_programId, "textureMatrix");
    m_flipHorizontalHandle = glGetUniformLocation(m_programId, "flipHorizontal");
    m_flipVerticalHandle = glGetUniformLocation(m_programId, "flipVertical");
}

const char* OES2RGBFilter::getVertexShaderSource() const {
    return kOESVertexShader;
}

const char* OES2RGBFilter::getFragmentShaderSource() const {
    return kOESFragmentShader;
}

void OES2RGBFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    glUseProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, inputTexture.id); // Important: Use OES target
    glUniform1i(m_inputImageTextureHandle, 0);

    if (m_parameters.count("textureMatrix") && m_parameters.at("textureMatrix").type() == typeid(std::vector<float>)) {
        auto matrix = std::any_cast<std::vector<float>>(m_parameters.at("textureMatrix"));
        if (matrix.size() == 16) {
            glUniformMatrix4fv(m_textureMatrixHandle, 1, GL_FALSE, matrix.data());
        }
    }

    bool flipH = false;
    if (m_parameters.count("flipHorizontal") && m_parameters.at("flipHorizontal").type() == typeid(bool)) {
        flipH = std::any_cast<bool>(m_parameters.at("flipHorizontal"));
    }
    glUniform1i(m_flipHorizontalHandle, flipH ? 1 : 0);

    bool flipV = false;
    if (m_parameters.count("flipVertical") && m_parameters.at("flipVertical").type() == typeid(bool)) {
        flipV = std::any_cast<bool>(m_parameters.at("flipVertical"));
    }
    glUniform1i(m_flipVerticalHandle, flipV ? 1 : 0);

    glEnableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    glEnableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(m_positionHandle);
    glDisableVertexAttribArray(m_texCoordHandle);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    outputFb->unbind();
}

// --- BrightnessFilter ---

BrightnessFilter::BrightnessFilter() {
    m_parameters["brightness"] = 0.0f; // Default brightness
}

void BrightnessFilter::initialize() {
    Filter::initialize();
    m_brightnessHandle = glGetUniformLocation(m_programId, "brightness");
}

const char* BrightnessFilter::getFragmentShaderSource() const {
    return kBrightnessFragmentShader;
}

void BrightnessFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    glUseProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    float brightness = 0.0f;
    if (m_parameters.count("brightness") && m_parameters.at("brightness").type() == typeid(float)) {
        brightness = std::any_cast<float>(m_parameters.at("brightness"));
    }
    glUniform1f(m_brightnessHandle, brightness);

    glEnableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    glEnableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(m_positionHandle);
    glDisableVertexAttribArray(m_texCoordHandle);
    glBindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}

// --- GaussianBlurFilter ---

// --- 高性能 Two-Pass 高斯模糊 (Gaussian Blur) ---

GaussianBlurFilter::GaussianBlurFilter(FrameBufferPool* pool) : m_pool(pool) {
    m_parameters["blurSize"] = 1.0f;
}

GaussianBlurFilter::~GaussianBlurFilter() {
}

void GaussianBlurFilter::initialize() {
    Filter::initialize();
    m_texelWidthOffsetHandle = glGetUniformLocation(m_programId, "texelWidthOffset");
    m_texelHeightOffsetHandle = glGetUniformLocation(m_programId, "texelHeightOffset");
    m_blurSizeHandle = glGetUniformLocation(m_programId, "blurSize");
}

Texture GaussianBlurFilter::processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (!m_pool) return inputTexture;

    // 从 FBO 池中借用一个与输入尺寸一致的临时 FrameBuffer 用于存放第一趟（水平模糊）的结果
    FrameBufferPtr intermediateFb = m_pool->get(inputTexture.width, inputTexture.height);

    // ---------------------------------------------------------
    // Pass 1: 水平模糊 (Horizontal Blur)
    // ---------------------------------------------------------
    intermediateFb->bind();
    glUseProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    // 设置水平方向的偏移量，垂直方向为 0
    glUniform1f(m_texelWidthOffsetHandle, 1.0f / inputTexture.width);
    glUniform1f(m_texelHeightOffsetHandle, 0.0f);

    float blurSize = 1.0f;
    if (m_parameters.count("blurSize")) {
        try { blurSize = std::any_cast<float>(m_parameters.at("blurSize")); } catch (...) {}
    }
    glUniform1f(m_blurSizeHandle, blurSize);

    // TODO: bind attributes correctly (assuming standard base class handles)
    static const float s_vertexCoords[] = { -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,  1.0f, 1.0f };
    static const float s_textureCoords[] = { 0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f };

    glEnableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    glEnableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // ---------------------------------------------------------
    // Pass 2: 垂直模糊 (Vertical Blur)
    // ---------------------------------------------------------
    outputFb->bind();
    glClear(GL_COLOR_BUFFER_BIT);

    // 绑定第一趟生成的临时纹理作为输入
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, intermediateFb->getTexture().id);
    glUniform1i(m_inputImageTextureHandle, 0);

    // 设置垂直方向的偏移量，水平方向为 0
    glUniform1f(m_texelWidthOffsetHandle, 0.0f);
    glUniform1f(m_texelHeightOffsetHandle, 1.0f / inputTexture.height);
    // blurSize 保持不变
    glUniform1f(m_blurSizeHandle, blurSize);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(m_positionHandle);
    glDisableVertexAttribArray(m_texCoordHandle);

    // 释放临时 FBO，归还到池中
    m_pool->release(intermediateFb);

    return outputFb->getTexture();
}

void GaussianBlurFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    // 留空，因为我们在重写的 processFrame 中已经完成了所有的绘制调度
}

const char* GaussianBlurFilter::getVertexShaderSource() const {
    // 顶点着色器预计算优化 (VS Pre-calculation):
    // 提前计算 5 个采样点的纹理坐标偏移。在 Vertex Shader 中做这件事，
    // 可以避免在 Fragment Shader 中针对每一个像素（可能数百万个）进行重复计算，极大地节省 ALU 算力。
    return R"(#version 300 es
        layout(location = 0) in vec4 a_position;
        layout(location = 1) in vec4 a_texCoord;

        uniform float texelWidthOffset;
        uniform float texelHeightOffset;
        uniform float blurSize;

        // 传递给片段着色器的 5 个采样点坐标（利用硬件双线性插值，这相当于采样了 9 个点）
        out vec2 blurCoordinates[5];

        void main() {
            gl_Position = a_position;
            vec2 singleStepOffset = vec2(texelWidthOffset, texelHeightOffset) * blurSize;

            // 当前像素点 (中心点)
            blurCoordinates[0] = a_texCoord.xy;
            // 距离中心 1.407333 像素的偏移（经过精心计算的硬件线性插值中心位置）
            blurCoordinates[1] = a_texCoord.xy + singleStepOffset * 1.407333;
            blurCoordinates[2] = a_texCoord.xy - singleStepOffset * 1.407333;
            // 距离中心 3.294215 像素的偏移
            blurCoordinates[3] = a_texCoord.xy + singleStepOffset * 3.294215;
            blurCoordinates[4] = a_texCoord.xy - singleStepOffset * 3.294215;
        }
    )";
}

const char* GaussianBlurFilter::getFragmentShaderSource() const {
    // 片段着色器:
    // 只需执行 5 次 texture 采样调用，利用不同的高斯权重(0.204164 等)进行叠加。
    // 这比传统的 O(N^2) for 循环采样快了上百倍。
    return R"(#version 300 es
        precision mediump float;

        uniform sampler2D inputImageTexture;
        in vec2 blurCoordinates[5];

        out vec4 fragColor;

        void main() {
            vec4 sum = vec4(0.0);

            // 中心点权重最高
            sum += texture(inputImageTexture, blurCoordinates[0]) * 0.204164;

            // 利用预计算好的偏移和硬件插值，单次 texture 相当于加权了 2 个周围像素点
            sum += texture(inputImageTexture, blurCoordinates[1]) * 0.304005;
            sum += texture(inputImageTexture, blurCoordinates[2]) * 0.304005;
            sum += texture(inputImageTexture, blurCoordinates[3]) * 0.093913;
            sum += texture(inputImageTexture, blurCoordinates[4]) * 0.093913;

            fragColor = sum;
        }
    )";
}

LookupFilter::LookupFilter() : m_lookupTextureId(0) {
    m_parameters["intensity"] = 1.0f;
    m_parameters["lookupTextureId"] = 0; // Support setting LUT texture via parameter
}

LookupFilter::~LookupFilter() {
}

void LookupFilter::initialize() {
    Filter::initialize();
    m_lookupTextureHandle = glGetUniformLocation(m_programId, "lookupTexture");
    m_intensityHandle = glGetUniformLocation(m_programId, "intensity");
}

void LookupFilter::setLookupTexture(GLuint textureId) {
    m_lookupTextureId = textureId;
}

const char* LookupFilter::getFragmentShaderSource() const {
    return kLookupFragmentShader;
}

void LookupFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    glUseProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    if (m_parameters.count("lookupTextureId") && m_parameters.at("lookupTextureId").type() == typeid(int)) {
        m_lookupTextureId = std::any_cast<int>(m_parameters.at("lookupTextureId"));
    }

    if (m_lookupTextureId) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_lookupTextureId);
        glUniform1i(m_lookupTextureHandle, 1);
    }

    float intensity = 1.0f;
    if (m_parameters.count("intensity") && m_parameters.at("intensity").type() == typeid(float)) {
        intensity = std::any_cast<float>(m_parameters.at("intensity"));
    }
    glUniform1f(m_intensityHandle, intensity);

    glEnableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    glEnableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(m_positionHandle);
    glDisableVertexAttribArray(m_texCoordHandle);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}

// --- BilateralFilter (Skin Smoothing) ---

BilateralFilter::BilateralFilter() {
    m_parameters["distanceNormalizationFactor"] = 8.0f; // Default smoothing factor
}

void BilateralFilter::initialize() {
    Filter::initialize();
    m_texelWidthOffsetHandle = glGetUniformLocation(m_programId, "texelWidthOffset");
    m_texelHeightOffsetHandle = glGetUniformLocation(m_programId, "texelHeightOffset");
    m_distanceNormalizationFactorHandle = glGetUniformLocation(m_programId, "distanceNormalizationFactor");
}

const char* BilateralFilter::getFragmentShaderSource() const {
    return kBilateralFragmentShader;
}

void BilateralFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    glUseProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    // Provide offsets for neighborhood sampling based on texture resolution
    float widthOffset = inputTexture.width > 0 ? (1.0f / inputTexture.width) : 0.0f;
    float heightOffset = inputTexture.height > 0 ? (1.0f / inputTexture.height) : 0.0f;

    // Increase offset slightly for more obvious smoothing spread
    glUniform1f(m_texelWidthOffsetHandle, widthOffset * 2.0f);
    glUniform1f(m_texelHeightOffsetHandle, heightOffset * 2.0f);

    float factor = 8.0f;
    if (m_parameters.count("distanceNormalizationFactor") && m_parameters.at("distanceNormalizationFactor").type() == typeid(float)) {
        factor = std::any_cast<float>(m_parameters.at("distanceNormalizationFactor"));
    }
    glUniform1f(m_distanceNormalizationFactorHandle, factor);

    glEnableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    glEnableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(m_positionHandle);
    glDisableVertexAttribArray(m_texCoordHandle);
    glBindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}

} // namespace video
} // namespace sdk

// --- CinematicLookupFilter Implementation ---
CinematicLookupFilter::CinematicLookupFilter()
    : m_lookupTextureHandle(0)
    , m_intensityHandle(0)
    , m_lookupTextureId(0)
{
    m_parameters["intensity"] = std::any(1.0f);
}

CinematicLookupFilter::~CinematicLookupFilter() {
    if (m_lookupTextureId != 0) {
        glDeleteTextures(1, &m_lookupTextureId);
        m_lookupTextureId = 0;
    }
}

void CinematicLookupFilter::setLookupTexture(GLuint textureId) {
    if (m_lookupTextureId != 0) {
        glDeleteTextures(1, &m_lookupTextureId);
    }
    m_lookupTextureId = textureId;
}

void CinematicLookupFilter::initialize() {
    Filter::initialize();
    m_lookupTextureHandle = glGetUniformLocation(m_programId, "lookupTexture");
    m_intensityHandle = glGetUniformLocation(m_programId, "intensity");
}

void CinematicLookupFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (m_lookupTextureId != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_lookupTextureId);
        glUniform1i(m_lookupTextureHandle, 1);
    }

    float intensity = 1.0f;
    auto it = m_parameters.find("intensity");
    if (it != m_parameters.end()) {
        try { intensity = std::any_cast<float>(it->second); } catch (...) {}
    }
    glUniform1f(m_intensityHandle, intensity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture.id);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindTexture(GL_TEXTURE_2D, 0);
}

const char* CinematicLookupFilter::getFragmentShaderSource() const {
    return R"(#version 300 es
        precision highp float;
        in vec2 v_texCoord;
        uniform sampler2D inputImageTexture;
        uniform sampler2D lookupTexture;
        uniform float intensity;

        out vec4 fragColor;

        void main() {
            vec4 textureColor = texture(inputImageTexture, v_texCoord);

            float blueColor = textureColor.b * 63.0;

            vec2 quad1;
            quad1.y = floor(floor(blueColor) / 8.0);
            quad1.x = floor(blueColor) - (quad1.y * 8.0);

            vec2 quad2;
            quad2.y = floor(ceil(blueColor) / 8.0);
            quad2.x = ceil(blueColor) - (quad2.y * 8.0);

            vec2 texPos1;
            texPos1.x = (quad1.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);
            texPos1.y = (quad1.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);

            vec2 texPos2;
            texPos2.x = (quad2.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);
            texPos2.y = (quad2.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);

            vec4 newColor1 = texture(lookupTexture, texPos1);
            vec4 newColor2 = texture(lookupTexture, texPos2);

            vec4 newColor = mix(newColor1, newColor2, fract(blueColor));

            fragColor = mix(textureColor, vec4(newColor.rgb, textureColor.w), intensity);
        }
    )";
}
