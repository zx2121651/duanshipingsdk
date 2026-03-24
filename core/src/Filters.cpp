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

GaussianBlurFilter::GaussianBlurFilter() {
    m_parameters["blurSize"] = 1.0f;
}

void GaussianBlurFilter::initialize() {
    Filter::initialize();
    m_texelWidthOffsetHandle = glGetUniformLocation(m_programId, "texelWidthOffset");
    m_texelHeightOffsetHandle = glGetUniformLocation(m_programId, "texelHeightOffset");
    m_blurSizeHandle = glGetUniformLocation(m_programId, "blurSize");
}

const char* GaussianBlurFilter::getFragmentShaderSource() const {
    return kGaussianBlurFragmentShader;
}

void GaussianBlurFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    glUseProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    // Simplistic single-pass diagonal blur passing offsets based on input texture dimensions
    glUniform1f(m_texelWidthOffsetHandle, 1.0f / inputTexture.width);
    glUniform1f(m_texelHeightOffsetHandle, 1.0f / inputTexture.height);

    float blurSize = 1.0f;
    if (m_parameters.count("blurSize") && m_parameters.at("blurSize").type() == typeid(float)) {
        blurSize = std::any_cast<float>(m_parameters.at("blurSize"));
    }
    glUniform1f(m_blurSizeHandle, blurSize);

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

// --- LookupFilter ---

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
