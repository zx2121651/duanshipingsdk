#include <array>
#include "../include/Filters.h"
#include "../include/GLStateManager.h"
#include <iostream>
#include <vector>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

namespace sdk {
namespace video {



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
    m_mat4Parameters["textureMatrix"] = std::array<float, 16>{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    m_parameters["flipHorizontal"] = false;
    m_parameters["flipVertical"] = false;
}

Result OES2RGBFilter::initialize() {
    auto res = Filter::initialize();
    if (!res.isOk()) return res;
    m_textureMatrixHandle = glGetUniformLocation(m_programId, "textureMatrix");
    m_flipHorizontalHandle = glGetUniformLocation(m_programId, "flipHorizontal");
    m_flipVerticalHandle = glGetUniformLocation(m_programId, "flipVertical");
    return Result::ok();
}

std::string OES2RGBFilter::getVertexShaderSource() const {
    return "";
}

void OES2RGBFilter::onProgramRecompiled() {
    m_textureMatrixHandle = glGetUniformLocation(m_programId, "textureMatrix");
    m_flipHorizontalHandle = glGetUniformLocation(m_programId, "flipHorizontal");
    m_flipVerticalHandle = glGetUniformLocation(m_programId, "flipVertical");
}
std::string OES2RGBFilter::getFragmentShaderSource() const {
    return "";
}

void OES2RGBFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_EXTERNAL_OES, inputTexture.id); // Important: Use OES target
    glUniform1i(m_inputImageTextureHandle, 0);

    if (m_mat4Parameters.count("textureMatrix")) {
        auto& matrix = m_mat4Parameters.at("textureMatrix");
        glUniformMatrix4fv(m_textureMatrixHandle, 1, GL_FALSE, matrix.data());
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

    GLStateManager::getInstance().enableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    GLStateManager::getInstance().enableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLStateManager::getInstance().disableVertexAttribArray(m_positionHandle);
    GLStateManager::getInstance().disableVertexAttribArray(m_texCoordHandle);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    outputFb->unbind();
}

// --- BrightnessFilter ---

BrightnessFilter::BrightnessFilter() {
    m_parameters["brightness"] = 0.0f; // Default brightness
}

Result BrightnessFilter::initialize() {
    auto res = Filter::initialize();
    if (!res.isOk()) return res;
    m_brightnessHandle = glGetUniformLocation(m_programId, "brightness");
    return Result::ok();
}

void BrightnessFilter::onProgramRecompiled() {
    m_brightnessHandle = glGetUniformLocation(m_programId, "brightness");
}
std::string BrightnessFilter::getFragmentShaderSource() const {
    return "";
}

void BrightnessFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    float brightness = 0.0f;
    if (m_parameters.count("brightness") && m_parameters.at("brightness").type() == typeid(float)) {
        brightness = std::any_cast<float>(m_parameters.at("brightness"));
    }
    glUniform1f(m_brightnessHandle, brightness);

    GLStateManager::getInstance().enableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    GLStateManager::getInstance().enableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLStateManager::getInstance().disableVertexAttribArray(m_positionHandle);
    GLStateManager::getInstance().disableVertexAttribArray(m_texCoordHandle);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}

// --- GaussianBlurFilter ---

// --- 高性能 Two-Pass 高斯模糊 (Gaussian Blur) ---

GaussianBlurFilter::GaussianBlurFilter(FrameBufferPool* pool) : m_pool(pool) {
    m_parameters["blurSize"] = 1.0f;
}

GaussianBlurFilter::~GaussianBlurFilter() {
}

Result GaussianBlurFilter::initialize() {
    auto res = Filter::initialize();
    if (!res.isOk()) return res;
    m_texelWidthOffsetHandle = glGetUniformLocation(m_programId, "texelWidthOffset");
    m_texelHeightOffsetHandle = glGetUniformLocation(m_programId, "texelHeightOffset");
    m_blurSizeHandle = glGetUniformLocation(m_programId, "blurSize");
    return Result::ok();
}

void GaussianBlurFilter::onProgramRecompiled() {
    m_texelWidthOffsetHandle = glGetUniformLocation(m_programId, "texelWidthOffset");
    m_texelHeightOffsetHandle = glGetUniformLocation(m_programId, "texelHeightOffset");
    m_blurSizeHandle = glGetUniformLocation(m_programId, "blurSize");
}
ResultPayload<Texture> GaussianBlurFilter::processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (!m_pool) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FrameBufferPool is null in GaussianBlurFilter");
    }

    // 从 FBO 池中借用一个与输入尺寸一致的临时 FrameBuffer 用于存放第一趟（水平模糊）的结果
    FrameBufferPtr intermediateFb = m_pool->get(inputTexture.width, inputTexture.height);
    if (!intermediateFb) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED, "Failed to allocate intermediate FBO for GaussianBlurFilter");
    }

    if (!outputFb) {
        m_pool->release(intermediateFb);
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Output framebuffer is null in GaussianBlurFilter");
    }

    // ---------------------------------------------------------
    // Pass 1: 水平模糊 (Horizontal Blur)
    // ---------------------------------------------------------
    intermediateFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    // 设置水平方向的偏移量，垂直方向为 0
    glUniform1f(m_texelWidthOffsetHandle, 1.0f / inputTexture.width);
    glUniform1f(m_texelHeightOffsetHandle, 0.0f);

    float blurSize = 1.0f;
    if (m_parameters.count("blurSize")) {
        try { blurSize = std::any_cast<float>(m_parameters.at("blurSize")); } catch (const std::bad_any_cast& e) { std::cerr << "Parameter type cast error: " << e.what() << std::endl; }
    }
    glUniform1f(m_blurSizeHandle, blurSize);

    // TODO: bind attributes correctly (assuming standard base class handles)
    static const float s_vertexCoords[] = { -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,  1.0f, 1.0f };
    static const float s_textureCoords[] = { 0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f };

    GLStateManager::getInstance().enableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    GLStateManager::getInstance().enableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // ---------------------------------------------------------
    // Pass 2: 垂直模糊 (Vertical Blur)
    // ---------------------------------------------------------
    outputFb->bind();
    glClear(GL_COLOR_BUFFER_BIT);

    // 绑定第一趟生成的临时纹理作为输入
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, intermediateFb->getTexture().id);
    glUniform1i(m_inputImageTextureHandle, 0);

    // 设置垂直方向的偏移量，水平方向为 0
    glUniform1f(m_texelWidthOffsetHandle, 0.0f);
    glUniform1f(m_texelHeightOffsetHandle, 1.0f / inputTexture.height);
    // blurSize 保持不变
    glUniform1f(m_blurSizeHandle, blurSize);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLStateManager::getInstance().disableVertexAttribArray(m_positionHandle);
    GLStateManager::getInstance().disableVertexAttribArray(m_texCoordHandle);

    // 释放临时 FBO，归还到池中
    m_pool->release(intermediateFb);

    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

void GaussianBlurFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    // 留空，因为我们在重写的 processFrame 中已经完成了所有的绘制调度
}

std::string GaussianBlurFilter::getVertexShaderSource() const {
    return "";
}

std::string GaussianBlurFilter::getFragmentShaderSource() const {
    return "";
}

LookupFilter::LookupFilter() : m_lookupTextureId(0) {
    m_parameters["intensity"] = 1.0f;
    m_parameters["lookupTextureId"] = 0; // Support setting LUT texture via parameter
}

LookupFilter::~LookupFilter() {
}

Result LookupFilter::initialize() {
    auto res = Filter::initialize();
    if (!res.isOk()) return res;
    m_lookupTextureHandle = glGetUniformLocation(m_programId, "lookupTexture");
    m_intensityHandle = glGetUniformLocation(m_programId, "intensity");
    return Result::ok();
}

void LookupFilter::setLookupTexture(GLuint textureId) {
    m_lookupTextureId = textureId;
}

void LookupFilter::onProgramRecompiled() {
    m_intensityHandle = glGetUniformLocation(m_programId, "intensity");
    m_lookupTextureHandle = glGetUniformLocation(m_programId, "lookupTexture");
}
std::string LookupFilter::getFragmentShaderSource() const {
    return "";
}

void LookupFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    if (m_parameters.count("lookupTextureId") && m_parameters.at("lookupTextureId").type() == typeid(int)) {
        m_lookupTextureId = std::any_cast<int>(m_parameters.at("lookupTextureId"));
    }

    if (m_lookupTextureId) {
        GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_lookupTextureId);
        glUniform1i(m_lookupTextureHandle, 1);
    }

    float intensity = 1.0f;
    if (m_parameters.count("intensity") && m_parameters.at("intensity").type() == typeid(float)) {
        intensity = std::any_cast<float>(m_parameters.at("intensity"));
    }
    glUniform1f(m_intensityHandle, intensity);

    GLStateManager::getInstance().enableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    GLStateManager::getInstance().enableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLStateManager::getInstance().disableVertexAttribArray(m_positionHandle);
    GLStateManager::getInstance().disableVertexAttribArray(m_texCoordHandle);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}

// --- BilateralFilter (Skin Smoothing) ---

BilateralFilter::BilateralFilter() {
    m_parameters["distanceNormalizationFactor"] = 8.0f; // Default smoothing factor
}

Result BilateralFilter::initialize() {
    auto res = Filter::initialize();
    if (!res.isOk()) return res;
    m_texelWidthOffsetHandle = glGetUniformLocation(m_programId, "texelWidthOffset");
    m_texelHeightOffsetHandle = glGetUniformLocation(m_programId, "texelHeightOffset");
    m_distanceNormalizationFactorHandle = glGetUniformLocation(m_programId, "distanceNormalizationFactor");
    return Result::ok();
}

void BilateralFilter::onProgramRecompiled() {
    m_texelWidthOffsetHandle = glGetUniformLocation(m_programId, "texelWidthOffset");
    m_texelHeightOffsetHandle = glGetUniformLocation(m_programId, "texelHeightOffset");
    m_distanceNormalizationFactorHandle = glGetUniformLocation(m_programId, "distanceNormalizationFactor");
}
std::string BilateralFilter::getFragmentShaderSource() const {
    return "";
}

void BilateralFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
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

    GLStateManager::getInstance().enableVertexAttribArray(m_positionHandle);
    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, s_vertexCoords);

    GLStateManager::getInstance().enableVertexAttribArray(m_texCoordHandle);
    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, s_textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLStateManager::getInstance().disableVertexAttribArray(m_positionHandle);
    GLStateManager::getInstance().disableVertexAttribArray(m_texCoordHandle);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}



// --- CinematicLookupFilter Implementation ---
// 这是电影级调色的滤镜，采用 GPUImage 风格的 512x512 2D 展开图来模拟 64x64x64 的 3D LUT (查找表)。
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

Result CinematicLookupFilter::initialize() {
    auto res = Filter::initialize();
    if (!res.isOk()) return res;
    m_lookupTextureHandle = glGetUniformLocation(m_programId, "lookupTexture");
    m_intensityHandle = glGetUniformLocation(m_programId, "intensity");
    return Result::ok();
}

void CinematicLookupFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (m_lookupTextureId != 0) {
        GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_lookupTextureId);
        glUniform1i(m_lookupTextureHandle, 1);
    }

    float intensity = 1.0f;
    auto it = m_parameters.find("intensity");
    if (it != m_parameters.end()) {
        try { intensity = std::any_cast<float>(it->second); } catch (const std::bad_any_cast& e) { std::cerr << "Parameter type cast error: " << e.what() << std::endl; }
    }
    glUniform1f(m_intensityHandle, intensity);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
}

void CinematicLookupFilter::onProgramRecompiled() {
    m_intensityHandle = glGetUniformLocation(m_programId, "intensity");
    m_lookupTextureHandle = glGetUniformLocation(m_programId, "lookupTexture");
}
std::string CinematicLookupFilter::getFragmentShaderSource() const {
    return "";
}


// --- NightVisionFilter Implementation ---
Result NightVisionFilter::initialize() {
    return Filter::initialize();
}

void NightVisionFilter::onProgramRecompiled() {
    // Nothing to do for dummy
}

void NightVisionFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    // TODO: bind attributes correctly (assuming standard base class handles)
    static const GLfloat squareVertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };
    static const GLfloat textureVertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    glVertexAttribPointer(m_positionHandle, 2, GL_FLOAT, GL_FALSE, 0, squareVertices);
    glEnableVertexAttribArray(m_positionHandle);

    glVertexAttribPointer(m_texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);
    glEnableVertexAttribArray(m_texCoordHandle);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(m_positionHandle);
    glDisableVertexAttribArray(m_texCoordHandle);
}

std::string NightVisionFilter::getFragmentShaderSource() const {
    return R"(
#version 300 es
precision mediump float;
in vec2 vTextureCoord;
out vec4 fragColor;
uniform sampler2D sTexture;

void main() {
    vec4 color = texture(sTexture, vTextureCoord);
    float luminance = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    fragColor = vec4(0.0, luminance, 0.0, color.a);
}
)";
}

#ifdef __ANDROID__
// --- 高性能 Compute Shader 计算模糊 (ComputeBlurFilter) ---

ComputeBlurFilter::ComputeBlurFilter() : m_computeProgramId(0) {
    m_parameters["blurSize"] = 2.0f; // 默认模糊半径
}

ComputeBlurFilter::~ComputeBlurFilter() {
    if (m_computeProgramId != 0) {
        glDeleteProgram(m_computeProgramId);
    }
}

Result ComputeBlurFilter::initialize() {
    // 1. 编译 Compute Shader
    std::string csStr = "";
    if (m_shaderManager) {
        csStr = m_shaderManager->getShaderSource(getComputeShaderName());
    }
    const char* csSrc = csStr.c_str();
    GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(computeShader, 1, &csSrc, NULL);
    glCompileShader(computeShader);

    GLint compiled = 0;
    glGetShaderiv(computeShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        std::string infoLogStr = "";
        GLint infoLen = 0;
        glGetShaderiv(computeShader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = new char[infoLen];
            glGetShaderInfoLog(computeShader, infoLen, NULL, infoLog);
            infoLogStr = infoLog;
            delete[] infoLog;
        }
        glDeleteShader(computeShader);
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED, "Error compiling compute shader: " + infoLogStr);
    }

    // 2. 链接 Program
    m_computeProgramId = glCreateProgram();
    glAttachShader(m_computeProgramId, computeShader);
    glLinkProgram(m_computeProgramId);

    GLint linked = 0;
    glGetProgramiv(m_computeProgramId, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(m_computeProgramId);
        m_computeProgramId = 0;
        glDeleteShader(computeShader);
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED, "Error linking compute program");
    }

    glDeleteShader(computeShader);

    m_blurSizeHandle = glGetUniformLocation(m_computeProgramId, "blurSize");
    return Result::ok();
}

ResultPayload<Texture> ComputeBlurFilter::processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (m_computeProgramId == 0) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "ComputeBlurFilter program not initialized");
    }

    if (!outputFb) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Output framebuffer is null in ComputeBlurFilter");
    }

    // 我们不需要调用 FBO 的 bind() 来画三角形。
    // Compute Shader 直接对着内存中的 Texture 进行读写（Image Store）。
    GLStateManager::getInstance().useProgram(m_computeProgramId);

    // 绑定输入纹理作为只读的 image2D (绑定在单元 0)
    glBindImageTexture(0, inputTexture.id, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

    // 绑定输出 FBO 的纹理作为只写的 image2D (绑定在单元 1)
    glBindImageTexture(1, outputFb->getTexture().id, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    float blurSize = 2.0f;
    if (m_parameters.count("blurSize")) {
        try { blurSize = std::any_cast<float>(m_parameters.at("blurSize")); } catch (const std::bad_any_cast& e) { std::cerr << "Parameter type cast error: " << e.what() << std::endl; }
    }
    glUniform1f(m_blurSizeHandle, blurSize);

    // 分发计算任务 (Dispatch)
    // 假设我们在 Shader 中定义了 local_size_x = 16, local_size_y = 16
    // 我们需要启动 (width/16) * (height/16) 个工作组 (Work Groups)
    GLuint numGroupsX = (inputTexture.width + 15) / 16;
    GLuint numGroupsY = (inputTexture.height + 15) / 16;

    glDispatchCompute(numGroupsX, numGroupsY, 1);

    // 设置内存屏障，确保在下次被当作纹理采样前，Compute Shader 的写入已经完全落盘到显存
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

void ComputeBlurFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    // 留空，重载 processFrame 直接使用 Compute
}

#endif

} // namespace video
} // namespace sdk
