#include <array>
#include "../include/Filters.h"
#include "../include/GLStateManager.h"
#include "rhi/GLTexture.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

namespace sdk {
namespace video {

namespace {

rhi::RenderPassDescriptor makeColorRenderPassDescriptor(
    const Texture& texture,
    rhi::LoadAction loadAction = rhi::LoadAction::Clear,
    rhi::Color clearColor = {0.0f, 0.0f, 0.0f, 1.0f}) {
    rhi::RenderPassDescriptor desc;
    rhi::RenderPassColorAttachment color;
    color.texture = std::make_shared<rhi::GLTexture>(texture.id, texture.width, texture.height);
    color.loadAction = loadAction;
    color.storeAction = rhi::StoreAction::Store;
    color.clearColor = clearColor;
    desc.colorAttachments.push_back(color);
    return desc;
}

std::shared_ptr<rhi::ITexture> wrapExistingTexture(
    const std::shared_ptr<rhi::IRenderDevice>& device,
    uint32_t textureId,
    uint32_t width,
    uint32_t height,
    uint32_t target = GL_TEXTURE_2D) {
    if (!device || textureId == 0 || width == 0 || height == 0) {
        return nullptr;
    }

    rhi::ExternalTextureDesc desc;
    desc.handle = textureId;
    desc.width = width;
    desc.height = height;
    desc.format = rhi::TextureFormat::RGBA8;
    desc.target = target;
    desc.ownsHandle = false;
    return device->wrapExternalTexture(desc);
}

std::shared_ptr<rhi::ITexture> wrapExistingTexture(
    const std::shared_ptr<rhi::IRenderDevice>& device,
    const Texture& texture,
    uint32_t target = GL_TEXTURE_2D) {
    return wrapExistingTexture(device, texture.id, texture.width, texture.height, target);
}

} // namespace



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

OES2RGBFilter::OES2RGBFilter()
    : m_textureMatrixHandle(static_cast<GLuint>(-1)),
      m_flipHorizontalHandle(static_cast<GLuint>(-1)),
      m_flipVerticalHandle(static_cast<GLuint>(-1))
{
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

    if (m_renderDevice) {
        m_paramsBuffer = m_renderDevice->createBuffer(rhi::BufferType::UniformBuffer, rhi::BufferUsage::DynamicDraw, sizeof(float) * 18, nullptr);
        rhi::PipelineStateDesc psoDesc;
        psoDesc.shaderProgram = nullptr;
        if (m_renderDevice->getCapabilities().backend != rhi::BackendType::GLES) {
            m_pipelineState = m_renderDevice->createGraphicsPipeline(psoDesc);
        }
    }
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

static std::array<float, 16> resolveTextureMatrix(
    const Texture& inputTexture,
    const std::map<std::string, std::array<float, 16>>& mat4Parameters) {
    if (inputTexture.source == rhi::ExternalTextureSource::GLOESTexture ||
        inputTexture.target == GL_TEXTURE_EXTERNAL_OES) {
        return inputTexture.transformMatrix;
    }

    auto it = mat4Parameters.find("textureMatrix");
    if (it != mat4Parameters.end()) {
        return it->second;
    }
    return inputTexture.transformMatrix;
}

void OES2RGBFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (!m_renderDevice || m_renderDevice->getCapabilities().backend == rhi::BackendType::GLES) {
        outputFb->bind();
        if (m_programId > 0) {
            GLStateManager::getInstance().useProgram(0);
            GLStateManager::getInstance().useProgram(m_programId);
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_EXTERNAL_OES, inputTexture.id);
        if (m_programId > 0 && m_inputImageTextureHandle != static_cast<GLuint>(-1)) {
            glUniform1i(m_inputImageTextureHandle, 0);
        }
        const auto matrix = resolveTextureMatrix(inputTexture, m_mat4Parameters);
        if (m_programId > 0 && m_textureMatrixHandle != static_cast<GLuint>(-1))
            glUniformMatrix4fv(m_textureMatrixHandle, 1, GL_FALSE, matrix.data());
        bool flipH = false;
        if (m_parameters.count("flipHorizontal") && m_parameters.at("flipHorizontal").type() == typeid(bool)) {
            flipH = std::any_cast<bool>(m_parameters.at("flipHorizontal"));
        }
        if (m_programId > 0 && m_flipHorizontalHandle != static_cast<GLuint>(-1))
            glUniform1i(m_flipHorizontalHandle, flipH ? 1 : 0);
        bool flipV = false;
        if (m_parameters.count("flipVertical") && m_parameters.at("flipVertical").type() == typeid(bool)) {
            flipV = std::any_cast<bool>(m_parameters.at("flipVertical"));
        }
        if (m_programId > 0 && m_flipVerticalHandle != static_cast<GLuint>(-1))
            glUniform1i(m_flipVerticalHandle, flipV ? 1 : 0);
        if (m_quadVao && m_renderDevice) {
            auto cmdBuffer = m_renderDevice->createCommandBuffer();
            if (cmdBuffer) {
                cmdBuffer->bindVertexArray(m_quadVao.get());
                cmdBuffer->draw(4);
            }
        }
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
        outputFb->unbind();
        return;
    }

    if (!m_pipelineState || !m_paramsBuffer || !m_quadVao) return;
    std::array<float, 16> matrix = resolveTextureMatrix(inputTexture, m_mat4Parameters);
    bool flipH = m_parameters.count("flipHorizontal") ? std::any_cast<bool>(m_parameters.at("flipHorizontal")) : false;
    bool flipV = m_parameters.count("flipVertical") ? std::any_cast<bool>(m_parameters.at("flipVertical")) : false;

    float bufferData[18];
    for (int i = 0; i < 16; i++) bufferData[i] = matrix[i];
    bufferData[16] = flipH ? 1.0f : 0.0f;
    bufferData[17] = flipV ? 1.0f : 0.0f;
    m_paramsBuffer->updateData(bufferData, sizeof(bufferData));

    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    if (!cmdBuffer) return;
    auto passDesc = makeColorRenderPassDescriptor(outputFb->getTexture());
    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, wrapExistingTexture(m_renderDevice, inputTexture, GL_TEXTURE_EXTERNAL_OES));
        resourceSet->bindUniformBuffer(1, m_paramsBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }
    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
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
    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    glUniform1i(m_inputImageTextureHandle, 0);

    float brightness = 0.0f;
    if (m_parameters.count("brightness") && m_parameters.at("brightness").type() == typeid(float)) {
        brightness = std::any_cast<float>(m_parameters.at("brightness"));
    }
    glUniform1f(m_brightnessHandle, brightness);

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

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

    if (m_renderDevice) {
        m_blurParamsBufferH = m_renderDevice->createBuffer(rhi::BufferType::UniformBuffer, rhi::BufferUsage::DynamicDraw, sizeof(float) * 4, nullptr);
        m_blurParamsBufferV = m_renderDevice->createBuffer(rhi::BufferType::UniformBuffer, rhi::BufferUsage::DynamicDraw, sizeof(float) * 4, nullptr);
        rhi::PipelineStateDesc psoDesc;
        psoDesc.shaderProgram = nullptr;
        if (m_renderDevice->getCapabilities().backend != rhi::BackendType::GLES) {
            m_pipelineState = m_renderDevice->createGraphicsPipeline(psoDesc);
        }
    }
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

    FrameBufferPtr intermediateFb = m_pool->get(inputTexture.width, inputTexture.height);
    if (!intermediateFb) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED, "Failed to allocate intermediate FBO for GaussianBlurFilter");
    }

    if (!outputFb) {
        m_pool->release(intermediateFb);
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Output framebuffer is null in GaussianBlurFilter");
    }

    float blurSize = 1.0f;
    if (m_parameters.count("blurSize")) {
        try { blurSize = std::any_cast<float>(m_parameters.at("blurSize")); } catch (const std::bad_any_cast& e) { }
    }

    if (!m_renderDevice || m_renderDevice->getCapabilities().backend == rhi::BackendType::GLES) {
        // --- Legacy Fallback ---
        intermediateFb->bind();
        if (m_programId > 0) GLStateManager::getInstance().useProgram(m_programId);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
        if (m_programId > 0) glUniform1i(m_inputImageTextureHandle, 0);

        if (m_programId > 0 && m_texelWidthOffsetHandle != (GLuint)-1) glUniform1f(m_texelWidthOffsetHandle, 1.0f / inputTexture.width);
        if (m_programId > 0 && m_texelHeightOffsetHandle != (GLuint)-1) glUniform1f(m_texelHeightOffsetHandle, 0.0f);
        if (m_programId > 0 && m_blurSizeHandle != (GLuint)-1) glUniform1f(m_blurSizeHandle, blurSize);

        if (m_quadVao && m_renderDevice) {
            auto cmdBuffer = m_renderDevice->createCommandBuffer();
            if (cmdBuffer) {
                cmdBuffer->bindVertexArray(m_quadVao.get());
                cmdBuffer->draw(4);
            }
        }

        outputFb->bind();
        glClear(GL_COLOR_BUFFER_BIT);

        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, intermediateFb->getTexture().id);
        if (m_programId > 0) glUniform1i(m_inputImageTextureHandle, 0);

        if (m_programId > 0 && m_texelWidthOffsetHandle != (GLuint)-1) glUniform1f(m_texelWidthOffsetHandle, 0.0f);
        if (m_programId > 0 && m_texelHeightOffsetHandle != (GLuint)-1) glUniform1f(m_texelHeightOffsetHandle, 1.0f / inputTexture.height);
        if (m_programId > 0 && m_blurSizeHandle != (GLuint)-1) glUniform1f(m_blurSizeHandle, blurSize);

        if (m_quadVao && m_renderDevice) {
            auto cmdBuffer2 = m_renderDevice->createCommandBuffer();
            if (cmdBuffer2) {
                cmdBuffer2->bindVertexArray(m_quadVao.get());
                cmdBuffer2->draw(4);
            }
        }

        m_pool->release(intermediateFb);
        return ResultPayload<Texture>::ok(outputFb->getTexture());
    }

    if (!m_pipelineState || !m_blurParamsBufferH || !m_blurParamsBufferV || !m_quadVao) {
        m_pool->release(intermediateFb);
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Missing RHI components");
    }

    float paramsH[4] = { 1.0f / inputTexture.width, 0.0f, blurSize, 0.0f };
    m_blurParamsBufferH->updateData(paramsH, sizeof(paramsH));

    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    if (!cmdBuffer) { m_pool->release(intermediateFb); return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "No command buffer"); }

    auto passDescH = makeColorRenderPassDescriptor(intermediateFb->getTexture());
    cmdBuffer->beginRenderPass(passDescH);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSetH = m_renderDevice->createShaderResourceSet();
    if (resourceSetH) {
        resourceSetH->bindTexture(0, wrapExistingTexture(m_renderDevice, inputTexture));
        resourceSetH->bindUniformBuffer(1, m_blurParamsBufferH);
        cmdBuffer->bindResourceSet(0, resourceSetH);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();

    cmdBuffer->pipelineBarrier(rhi::BarrierType::Pipeline);

    float paramsV[4] = { 0.0f, 1.0f / inputTexture.height, blurSize, 0.0f };
    m_blurParamsBufferV->updateData(paramsV, sizeof(paramsV));

    auto passDescV = makeColorRenderPassDescriptor(outputFb->getTexture());
    cmdBuffer->beginRenderPass(passDescV);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSetV = m_renderDevice->createShaderResourceSet();
    if (resourceSetV) {
        resourceSetV->bindTexture(0, std::make_shared<rhi::GLTexture>(intermediateFb->getTexture().id, intermediateFb->getTexture().width, intermediateFb->getTexture().height));
        resourceSetV->bindUniformBuffer(1, m_blurParamsBufferV);
        cmdBuffer->bindResourceSet(0, resourceSetV);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();

    m_renderDevice->submit(cmdBuffer.get());

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

    if (m_renderDevice) {
        m_intensityBuffer = m_renderDevice->createBuffer(rhi::BufferType::UniformBuffer, rhi::BufferUsage::DynamicDraw, sizeof(float), nullptr);
        rhi::PipelineStateDesc psoDesc;
        psoDesc.shaderProgram = nullptr;
        if (m_renderDevice->getCapabilities().backend != rhi::BackendType::GLES) {
            m_pipelineState = m_renderDevice->createGraphicsPipeline(psoDesc);
        }
    }
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
    if (!m_renderDevice || m_renderDevice->getCapabilities().backend == rhi::BackendType::GLES) {
        outputFb->bind();
        if (m_programId > 0) {
            GLStateManager::getInstance().useProgram(m_programId);
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);

        if (m_programId > 0) {
            glUniform1i(m_inputImageTextureHandle, 0);
        }

        if (m_parameters.count("lookupTextureId") && m_parameters.at("lookupTextureId").type() == typeid(int)) {
            m_lookupTextureId = std::any_cast<int>(m_parameters.at("lookupTextureId"));
        }

        if (m_lookupTextureId) {
            GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
            GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_lookupTextureId);
            if (m_programId > 0 && m_lookupTextureHandle != (GLuint)-1) glUniform1i(m_lookupTextureHandle, 1);
        }

        float intensity = 1.0f;
        if (m_parameters.count("intensity") && m_parameters.at("intensity").type() == typeid(float)) {
            intensity = std::any_cast<float>(m_parameters.at("intensity"));
        }
        if (m_programId > 0 && m_intensityHandle != (GLuint)-1) glUniform1f(m_intensityHandle, intensity);

        if (m_quadVao && m_renderDevice) {
            auto cmdBuffer = m_renderDevice->createCommandBuffer();
            if (cmdBuffer) {
                cmdBuffer->bindVertexArray(m_quadVao.get());
                cmdBuffer->draw(4);
            }
        }

        GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

        outputFb->unbind();
        return;
    }

    if (!m_pipelineState || !m_intensityBuffer || !m_quadVao) return;

    if (m_parameters.count("lookupTextureId") && m_parameters.at("lookupTextureId").type() == typeid(int)) {
        m_lookupTextureId = std::any_cast<int>(m_parameters.at("lookupTextureId"));
    }

    float intensity = 1.0f;
    if (m_parameters.count("intensity") && m_parameters.at("intensity").type() == typeid(float)) {
        intensity = std::any_cast<float>(m_parameters.at("intensity"));
    }

    m_intensityBuffer->updateData(&intensity, sizeof(float));

    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    if (!cmdBuffer) return;

    auto passDesc = makeColorRenderPassDescriptor(outputFb->getTexture());
    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, wrapExistingTexture(m_renderDevice, inputTexture));
        if (m_lookupTextureId) {
            resourceSet->bindTexture(1, wrapExistingTexture(m_renderDevice, m_lookupTextureId, 512, 512));
        }
        resourceSet->bindUniformBuffer(2, m_intensityBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
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

    if (m_renderDevice) {
        m_paramsBuffer = m_renderDevice->createBuffer(rhi::BufferType::UniformBuffer, rhi::BufferUsage::DynamicDraw, sizeof(float) * 3, nullptr);
        rhi::PipelineStateDesc psoDesc;
        psoDesc.shaderProgram = nullptr;
        if (m_renderDevice->getCapabilities().backend != rhi::BackendType::GLES) {
            m_pipelineState = m_renderDevice->createGraphicsPipeline(psoDesc);
        }
    }
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
    float widthOffset = inputTexture.width > 0 ? (1.0f / inputTexture.width) : 0.0f;
    float heightOffset = inputTexture.height > 0 ? (1.0f / inputTexture.height) : 0.0f;
    float factor = 8.0f;
    if (m_parameters.count("distanceNormalizationFactor") && m_parameters.at("distanceNormalizationFactor").type() == typeid(float)) {
        factor = std::any_cast<float>(m_parameters.at("distanceNormalizationFactor"));
    }

    if (!m_renderDevice || m_renderDevice->getCapabilities().backend == rhi::BackendType::GLES) {
        outputFb->bind();
        if (m_programId > 0) GLStateManager::getInstance().useProgram(m_programId);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);

        if (m_programId > 0) {
            glUniform1i(m_inputImageTextureHandle, 0);
            if (m_texelWidthOffsetHandle != (GLuint)-1) glUniform1f(m_texelWidthOffsetHandle, widthOffset * 2.0f);
            if (m_texelHeightOffsetHandle != (GLuint)-1) glUniform1f(m_texelHeightOffsetHandle, heightOffset * 2.0f);
            if (m_distanceNormalizationFactorHandle != (GLuint)-1) glUniform1f(m_distanceNormalizationFactorHandle, factor);
        }

        if (m_quadVao && m_renderDevice) {
            auto cmdBuffer = m_renderDevice->createCommandBuffer();
            if (cmdBuffer) {
                cmdBuffer->bindVertexArray(m_quadVao.get());
                cmdBuffer->draw(4);
            }
        }

        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
        outputFb->unbind();
        return;
    }

    if (!m_pipelineState || !m_paramsBuffer || !m_quadVao) return;

    float params[3] = { widthOffset * 2.0f, heightOffset * 2.0f, factor };
    m_paramsBuffer->updateData(params, sizeof(params));

    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    if (!cmdBuffer) return;

    auto passDesc = makeColorRenderPassDescriptor(outputFb->getTexture());
    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, wrapExistingTexture(m_renderDevice, inputTexture));
        resourceSet->bindUniformBuffer(1, m_paramsBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
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

    if (m_renderDevice) {
        m_intensityBuffer = m_renderDevice->createBuffer(rhi::BufferType::UniformBuffer, rhi::BufferUsage::DynamicDraw, sizeof(float), nullptr);
        rhi::PipelineStateDesc psoDesc;
        psoDesc.shaderProgram = nullptr;
        if (m_renderDevice->getCapabilities().backend != rhi::BackendType::GLES) {
            m_pipelineState = m_renderDevice->createGraphicsPipeline(psoDesc);
        }
    }
    return Result::ok();
}

void CinematicLookupFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (!m_renderDevice || m_renderDevice->getCapabilities().backend == rhi::BackendType::GLES) {
        outputFb->bind();
        if (m_programId > 0) {
            GLStateManager::getInstance().useProgram(m_programId);
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);

        if (m_programId > 0) {
            glUniform1i(m_inputImageTextureHandle, 0);
        }

        if (m_lookupTextureId != 0) {
            GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
            GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_lookupTextureId);
            if (m_programId > 0 && m_lookupTextureHandle != (GLuint)-1) glUniform1i(m_lookupTextureHandle, 1);
        }

        float intensity = 1.0f;
        auto it = m_parameters.find("intensity");
        if (it != m_parameters.end()) {
            try { intensity = std::any_cast<float>(it->second); } catch (const std::bad_any_cast& e) { }
        }
        if (m_programId > 0 && m_intensityHandle != (GLuint)-1) glUniform1f(m_intensityHandle, intensity);

        if (m_quadVao && m_renderDevice) {
            auto cmdBuffer = m_renderDevice->createCommandBuffer();
            if (cmdBuffer) {
                cmdBuffer->bindVertexArray(m_quadVao.get());
                cmdBuffer->draw(4);
            }
        }

        GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

        outputFb->unbind();
        return;
    }

    if (!m_pipelineState || !m_intensityBuffer || !m_quadVao) return;

    float intensity = 1.0f;
    auto it = m_parameters.find("intensity");
    if (it != m_parameters.end()) {
        try { intensity = std::any_cast<float>(it->second); } catch (const std::bad_any_cast& e) { }
    }

    m_intensityBuffer->updateData(&intensity, sizeof(float));

    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    if (!cmdBuffer) return;

    auto passDesc = makeColorRenderPassDescriptor(outputFb->getTexture());
    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, wrapExistingTexture(m_renderDevice, inputTexture));

        if (m_lookupTextureId != 0) {
            resourceSet->bindTexture(1, wrapExistingTexture(m_renderDevice, m_lookupTextureId, 512, 512));
        }
        resourceSet->bindUniformBuffer(2, m_intensityBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
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
    auto outputPass = beginOutputRenderPass(outputFb);
    GLStateManager::getInstance().useProgram(m_programId);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_inputImageTextureHandle, 0);

    auto cmdBuffer = outputPass.commandBuffer;
    if (!cmdBuffer && m_renderDevice) {
        cmdBuffer = m_renderDevice->createCommandBuffer();
    }
    if (cmdBuffer && m_quadVao) {
        cmdBuffer->bindVertexArray(m_quadVao.get());
        cmdBuffer->draw(4);
    }

    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
    endOutputRenderPass(outputPass, outputFb);
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

// --- LUT3DFilter Implementation ---

LUT3DFilter::LUT3DFilter() {
    m_parameters["intensity"] = 1.0f;
}

LUT3DFilter::~LUT3DFilter() {
    if (m_lut3dTexture != 0) {
        glDeleteTextures(1, &m_lut3dTexture);
        m_lut3dTexture = 0;
    }
}

Result LUT3DFilter::initialize() {
    Result base = Filter::initialize();
    if (!base.isOk()) return base;

    glGenTextures(1, &m_lut3dTexture);
    glBindTexture(GL_TEXTURE_3D, m_lut3dTexture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    std::vector<uint8_t> identity(LUT_SIZE * LUT_SIZE * LUT_SIZE * 3);
    for (int b = 0; b < LUT_SIZE; b++)
        for (int g = 0; g < LUT_SIZE; g++)
            for (int r = 0; r < LUT_SIZE; r++) {
                int idx = (b * LUT_SIZE * LUT_SIZE + g * LUT_SIZE + r) * 3;
                identity[idx + 0] = static_cast<uint8_t>(r * 255 / (LUT_SIZE - 1));
                identity[idx + 1] = static_cast<uint8_t>(g * 255 / (LUT_SIZE - 1));
                identity[idx + 2] = static_cast<uint8_t>(b * 255 / (LUT_SIZE - 1));
            }
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, LUT_SIZE, LUT_SIZE, LUT_SIZE,
                 0, GL_RGB, GL_UNSIGNED_BYTE, identity.data());
    glBindTexture(GL_TEXTURE_3D, 0);
    return Result::ok();
}

void LUT3DFilter::onProgramRecompiled() {
    m_lut3dHandle     = glGetUniformLocation(m_programId, "lut3d");
    m_intensityHandle = glGetUniformLocation(m_programId, "uIntensity");
}

void LUT3DFilter::setLUT(const uint8_t* rgbData, int size) {
    const int expected = LUT_SIZE * LUT_SIZE * LUT_SIZE * 3;
    if (!rgbData || size < expected) return;
    glBindTexture(GL_TEXTURE_3D, m_lut3dTexture);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, LUT_SIZE, LUT_SIZE, LUT_SIZE,
                 0, GL_RGB, GL_UNSIGNED_BYTE, rgbData);
    glBindTexture(GL_TEXTURE_3D, 0);
}

void LUT3DFilter::setIntensity(float intensity) {
    m_intensity = std::max(0.0f, std::min(1.0f, intensity));
    m_parameters["intensity"] = m_intensity;
}

void LUT3DFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    glUniform1i(m_inputImageTextureHandle, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, m_lut3dTexture);
    glUniform1i(m_lut3dHandle, 1);
    glUniform1f(m_intensityHandle, m_intensity);

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, 0);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}

std::string LUT3DFilter::getFragmentShaderSource() const {
    return R"(#version 300 es
precision mediump float;
in vec2 vTextureCoord;
out vec4 fragColor;
uniform sampler2D  sTexture;
uniform highp sampler3D lut3d;
uniform float      uIntensity;
void main() {
    vec4 src = texture(sTexture, vTextureCoord);
    float scale  = 63.0 / 64.0;
    float offset = 0.5  / 64.0;
    vec3 lutCoord = src.rgb * scale + offset;
    vec3 graded = texture(lut3d, lutCoord).rgb;
    fragColor = vec4(mix(src.rgb, graded, uIntensity), src.a);
}
)";
}

#ifdef __ANDROID__
// ---------------------------------------------------------------------------
// ComputeBlurFilter — 两阶段可分离高斯模糊 (Separable Gaussian, GLES 3.1)
//
// Pass 1 (H): inputTexture  → m_tempTexId  (水平卷积, local_size_x=64)
// Pass 2 (V): m_tempTexId   → outputFb     (垂直卷积, local_size_y=64)
//
// 相较原 O(r²) 盒式模糊：
//   - 每像素 imageLoad 次数从 (2r+1)² 降至 2*(2r+1)
//   - shared memory tile 消除重复全局显存读取
//   - r=15, 1080p 时显存带宽节省约 16×
// ---------------------------------------------------------------------------

ComputeBlurFilter::ComputeBlurFilter() {
    m_parameters["blurSize"] = 2.0f;
}

ComputeBlurFilter::~ComputeBlurFilter() {
    if (m_computeProgramH != 0) glDeleteProgram(m_computeProgramH);
    if (m_computeProgramV != 0) glDeleteProgram(m_computeProgramV);
    if (m_tempTexId       != 0) glDeleteTextures(1, &m_tempTexId);
}

// ---------------------------------------------------------------------------
// Helper: compile + link one compute shader from ShaderManager
// ---------------------------------------------------------------------------
Result ComputeBlurFilter::compileComputeProgram(const std::string& shaderName, GLuint& outProgId) {
    std::string src;
    if (m_shaderManager) {
        src = m_shaderManager->getShaderSource(shaderName);
    }
    if (src.empty()) {
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED,
            "compute shader source empty: " + shaderName);
    }

    const char* csSrc = src.c_str();
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &csSrc, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? len : 1, '\0');
        if (len > 1) glGetShaderInfoLog(shader, len, nullptr, &log[0]);
        glDeleteShader(shader);
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED,
            "compile " + shaderName + ": " + log);
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader);
    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    glDeleteShader(shader);
    if (!linked) {
        glDeleteProgram(prog);
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED,
            "link " + shaderName + " failed");
    }

    outProgId = prog;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// Helper: normalised Gaussian weights for radius (σ = radius/2)
// ---------------------------------------------------------------------------
void ComputeBlurFilter::recomputeWeights(int radius) {
    if (radius == m_lastRadius) return;
    m_lastRadius = radius;

    float sigma = std::max(radius / 2.0f, 0.5f);
    float sum   = 0.0f;
    for (int i = 0; i <= radius; i++) {
        m_weights[i] = std::exp(-0.5f * i * i / (sigma * sigma));
        sum += (i == 0) ? m_weights[i] : 2.0f * m_weights[i];
    }
    for (int i = 0; i <= radius; i++) m_weights[i] /= sum;
}

// ---------------------------------------------------------------------------
// Helper: create (or resize) temp texture used as H-pass output
// ---------------------------------------------------------------------------
void ComputeBlurFilter::ensureTempTexture(int width, int height) {
    if (m_tempTexId != 0 && m_tempTexWidth == width && m_tempTexHeight == height) return;

    if (m_tempTexId != 0) glDeleteTextures(1, &m_tempTexId);

    glGenTextures(1, &m_tempTexId);
    glBindTexture(GL_TEXTURE_2D, m_tempTexId);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_tempTexWidth  = width;
    m_tempTexHeight = height;
}

// ---------------------------------------------------------------------------
// initialize(): compile H and V programs, cache uniform locations
// ---------------------------------------------------------------------------
Result ComputeBlurFilter::initialize() {
    Result res = compileComputeProgram(getComputeShaderNameH(), m_computeProgramH);
    if (!res.isOk()) return res;

    res = compileComputeProgram(getComputeShaderNameV(), m_computeProgramV);
    if (!res.isOk()) return res;

    m_radiusHandleH  = glGetUniformLocation(m_computeProgramH, "u_radius");
    m_weightsHandleH = glGetUniformLocation(m_computeProgramH, "u_weights");
    m_radiusHandleV  = glGetUniformLocation(m_computeProgramV, "u_radius");
    m_weightsHandleV = glGetUniformLocation(m_computeProgramV, "u_weights");

    return Result::ok();
}

// ---------------------------------------------------------------------------
// processFrame(): H dispatch → barrier → V dispatch → barrier
// ---------------------------------------------------------------------------
ResultPayload<Texture> ComputeBlurFilter::processFrame(const Texture& inputTexture,
                                                        FrameBufferPtr outputFb) {
    if (m_computeProgramH == 0 || m_computeProgramV == 0) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE,
            "ComputeBlurFilter: programs not initialized");
    }
    if (!outputFb) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE,
            "ComputeBlurFilter: null outputFb");
    }

    const int W = inputTexture.width;
    const int H = inputTexture.height;

    // Radius from blurSize parameter (clamped to MAX_RADIUS)
    float blurSize = 2.0f;
    if (m_parameters.count("blurSize")) {
        try { blurSize = std::any_cast<float>(m_parameters.at("blurSize")); }
        catch (const std::bad_any_cast&) {}
    }
    int radius = std::min(static_cast<int>(blurSize), MAX_RADIUS);
    radius = std::max(radius, 1);

    recomputeWeights(radius);
    ensureTempTexture(W, H);

    // ----------------------------------------------------------------
    // Pass 1: Horizontal  —  inputTexture → m_tempTexId
    // ----------------------------------------------------------------
    GLStateManager::getInstance().useProgram(m_computeProgramH);

    glBindImageTexture(0, inputTexture.id, 0, GL_FALSE, 0, GL_READ_ONLY,  GL_RGBA8);
    glBindImageTexture(1, m_tempTexId,     0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    glUniform1i(m_radiusHandleH, radius);
    glUniform1fv(m_weightsHandleH, radius + 1, m_weights);

    // H pass: one workgroup per 64-pixel row segment
    GLuint groupsX = (static_cast<GLuint>(W) + 63u) / 64u;
    GLuint groupsY = static_cast<GLuint>(H);
    glDispatchCompute(groupsX, groupsY, 1);

    // Ensure H-pass writes are visible to V-pass reads
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // ----------------------------------------------------------------
    // Pass 2: Vertical  —  m_tempTexId → outputFb texture
    // ----------------------------------------------------------------
    GLStateManager::getInstance().useProgram(m_computeProgramV);

    glBindImageTexture(0, m_tempTexId,              0, GL_FALSE, 0, GL_READ_ONLY,  GL_RGBA8);
    glBindImageTexture(1, outputFb->getTexture().id, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    glUniform1i(m_radiusHandleV, radius);
    glUniform1fv(m_weightsHandleV, radius + 1, m_weights);

    // V pass: one workgroup per 64-pixel column segment
    GLuint groupsX2 = static_cast<GLuint>(W);
    GLuint groupsY2 = (static_cast<GLuint>(H) + 63u) / 64u;
    glDispatchCompute(groupsX2, groupsY2, 1);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

void ComputeBlurFilter::onDraw(const Texture& /*inputTexture*/, FrameBufferPtr /*outputFb*/) {
    // Not used: processFrame overrides the full pipeline
}

#endif

// ============================================================================
// Shared inline shader sources for Dual Kawase Blur and Bloom
// ============================================================================

namespace {

static const char* kKawaseVertSrc = R"(
#version 300 es
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texCoord;
out vec2 v_texCoord;
void main() { gl_Position = position; v_texCoord = texCoord; }
)";

static const char* kKawaseFragSrc = R"(
#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D inputImageTexture;
uniform vec2  u_texelSize;
uniform float u_offset;
out vec4 fragColor;
void main() {
    float d = u_offset;
    vec4 s  = texture(inputImageTexture, v_texCoord + vec2(-d,-d) * u_texelSize);
    s      += texture(inputImageTexture, v_texCoord + vec2(-d, d) * u_texelSize);
    s      += texture(inputImageTexture, v_texCoord + vec2( d,-d) * u_texelSize);
    s      += texture(inputImageTexture, v_texCoord + vec2( d, d) * u_texelSize);
    fragColor = s * 0.25;
}
)";

static const char* kBloomThreshFragSrc = R"(
#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D inputImageTexture;
uniform float u_threshold;
uniform float u_knee;
out vec4 fragColor;
void main() {
    vec3  color = texture(inputImageTexture, v_texCoord).rgb;
    float luma  = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float rq = clamp(luma - u_threshold + u_knee, 0.0, 2.0 * u_knee);
    rq       = (rq * rq) / (4.0 * u_knee + 1.0e-5);
    float w  = max(rq, luma - u_threshold) / max(luma, 1.0e-4);
    fragColor = vec4(color * w, 1.0);
}
)";

static const char* kBloomCompFragSrc = R"(
#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D inputImageTexture;
uniform sampler2D u_bloomTex;
uniform float     u_intensity;
out vec4 fragColor;
void main() {
    vec3 orig  = texture(inputImageTexture, v_texCoord).rgb;
    vec3 bloom = texture(u_bloomTex,        v_texCoord).rgb;
    fragColor  = vec4(clamp(orig + bloom * u_intensity, 0.0, 1.0), 1.0);
}
)";

} // anonymous namespace

// ============================================================================
// DualKawaseBlurFilter
// ============================================================================

DualKawaseBlurFilter::DualKawaseBlurFilter(FrameBufferPool* pool)
    : m_pool(pool) {}

DualKawaseBlurFilter::~DualKawaseBlurFilter() {
    DualKawaseBlurFilter::release();
}

void DualKawaseBlurFilter::release() {
    // m_programId is released by Filter::release()
    Filter::release();
}

Result DualKawaseBlurFilter::initialize() {
    auto res = Filter::initialize();
    if (!res.isOk()) return res;
    cacheUniforms();
    return Result::ok();
}

void DualKawaseBlurFilter::onProgramRecompiled() {
    cacheUniforms();
}

void DualKawaseBlurFilter::cacheUniforms() {
    m_locTexelSize = glGetUniformLocation(m_programId, "u_texelSize");
    m_locOffset    = glGetUniformLocation(m_programId, "u_offset");
}

std::string DualKawaseBlurFilter::getVertexShaderSource() const {
    return kKawaseVertSrc;
}

std::string DualKawaseBlurFilter::getFragmentShaderSource() const {
    return kKawaseFragSrc;
}

void DualKawaseBlurFilter::drawQuad() {
    if (m_renderDevice && m_quadVao) {
        auto cmd = m_renderDevice->createCommandBuffer();
        cmd->bindVertexArray(m_quadVao.get());
        cmd->draw(4);
    } else {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

ResultPayload<Texture> DualKawaseBlurFilter::processFrame(
    const Texture& inputTexture, FrameBufferPtr outputFb)
{
    if (m_programId == 0)
        return ResultPayload<Texture>::error(
            ErrorCode::ERR_RENDER_INVALID_STATE, "DualKawaseBlurFilter: not initialized");
    if (!outputFb)
        return ResultPayload<Texture>::error(
            ErrorCode::ERR_RENDER_INVALID_STATE, "DualKawaseBlurFilter: null outputFb");

    // Read parameters
    if (m_parameters.count("iterations"))
        m_iterations = std::max(1, std::min(std::any_cast<int>(m_parameters.at("iterations")), kMaxIterations));
    if (m_parameters.count("blurOffset"))
        m_blurOffset = std::max(0.1f, std::any_cast<float>(m_parameters.at("blurOffset")));

    const int W = static_cast<int>(inputTexture.width);
    const int H = static_cast<int>(inputTexture.height);

    // Acquire two ping-pong FBOs from the pool
    auto fboA = m_pool->get(W, H);
    auto fboB = m_pool->get(W, H);
    if (!fboA || !fboB)
        return ResultPayload<Texture>::error(
            ErrorCode::ERR_RENDER_INVALID_STATE, "DualKawaseBlurFilter: pool exhausted");

    GLStateManager::getInstance().useProgram(m_programId);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(m_inputImageTextureHandle, 0);
    glUniform2f(m_locTexelSize, 1.0f / W, 1.0f / H);

    GLuint srcTex = inputTexture.id;

    for (int i = 0; i < m_iterations; ++i) {
        const bool isLast = (i == m_iterations - 1);
        FrameBufferPtr dst = isLast ? outputFb : ((i % 2 == 0) ? fboA : fboB);

        dst->bind();
        glViewport(0, 0, W, H);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, srcTex);
        glUniform1f(m_locOffset, (i + 0.5f) * m_blurOffset);
        drawQuad();
        dst->unbind();

        if (!isLast)
            srcTex = dst->getTexture().id;
    }

    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

// ============================================================================
// BloomFilter
// ============================================================================

BloomFilter::BloomFilter(FrameBufferPool* pool)
    : m_pool(pool) {}

BloomFilter::~BloomFilter() {
    BloomFilter::release();
}

void BloomFilter::release() {
    if (m_kawaseProg) { glDeleteProgram(m_kawaseProg); m_kawaseProg = 0; }
    if (m_compProg)   { glDeleteProgram(m_compProg);   m_compProg   = 0; }
    Filter::release();
}

Result BloomFilter::initialize() {
    // Filter::initialize() compiles m_programId = threshold program
    auto res = Filter::initialize();
    if (!res.isOk()) return res;

    m_locThreshold = glGetUniformLocation(m_programId, "u_threshold");
    m_locKnee      = glGetUniformLocation(m_programId, "u_knee");

    // Compile kawase program
    m_kawaseProg = createProgram(kKawaseVertSrc, kKawaseFragSrc);
    if (!m_kawaseProg)
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED, "BloomFilter: kawase program failed");
    m_locKawaseInputTex  = glGetUniformLocation(m_kawaseProg, "inputImageTexture");
    m_locKawaseTexelSize = glGetUniformLocation(m_kawaseProg, "u_texelSize");
    m_locKawaseOffset    = glGetUniformLocation(m_kawaseProg, "u_offset");

    // Compile composite program
    m_compProg = createProgram(kKawaseVertSrc, kBloomCompFragSrc);
    if (!m_compProg)
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED, "BloomFilter: composite program failed");
    m_locCompInputTex = glGetUniformLocation(m_compProg, "inputImageTexture");
    m_locBloomTex     = glGetUniformLocation(m_compProg, "u_bloomTex");
    m_locIntensity    = glGetUniformLocation(m_compProg, "u_intensity");

    return Result::ok();
}

std::string BloomFilter::getVertexShaderSource() const {
    return kKawaseVertSrc;
}

std::string BloomFilter::getFragmentShaderSource() const {
    return kBloomThreshFragSrc;
}

void BloomFilter::drawQuad() {
    if (m_renderDevice && m_quadVao) {
        auto cmd = m_renderDevice->createCommandBuffer();
        cmd->bindVertexArray(m_quadVao.get());
        cmd->draw(4);
    } else {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

ResultPayload<Texture> BloomFilter::processFrame(
    const Texture& inputTexture, FrameBufferPtr outputFb)
{
    if (!m_programId || !m_kawaseProg || !m_compProg)
        return ResultPayload<Texture>::error(
            ErrorCode::ERR_RENDER_INVALID_STATE, "BloomFilter: not initialized");
    if (!outputFb)
        return ResultPayload<Texture>::error(
            ErrorCode::ERR_RENDER_INVALID_STATE, "BloomFilter: null outputFb");

    // Read parameters
    float threshold = 0.8f, knee = 0.1f, intensity = 0.6f;
    int   iterations = 4;
    if (m_parameters.count("threshold"))  threshold  = std::any_cast<float>(m_parameters.at("threshold"));
    if (m_parameters.count("knee"))       knee       = std::any_cast<float>(m_parameters.at("knee"));
    if (m_parameters.count("intensity"))  intensity  = std::any_cast<float>(m_parameters.at("intensity"));
    if (m_parameters.count("iterations")) iterations = std::any_cast<int>  (m_parameters.at("iterations"));
    iterations = std::max(1, std::min(iterations, 8));

    const int W = static_cast<int>(inputTexture.width);
    const int H = static_cast<int>(inputTexture.height);

    // Acquire 3 FBOs: threshold result + 2 for kawase ping-pong
    auto threshFbo = m_pool->get(W, H);
    auto blurFboA  = m_pool->get(W, H);
    auto blurFboB  = m_pool->get(W, H);
    if (!threshFbo || !blurFboA || !blurFboB)
        return ResultPayload<Texture>::error(
            ErrorCode::ERR_RENDER_INVALID_STATE, "BloomFilter: pool exhausted");

    // --- Pass 1: Threshold extraction ---
    {
        threshFbo->bind();
        glViewport(0, 0, W, H);
        GLStateManager::getInstance().useProgram(m_programId);
        glActiveTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
        glUniform1i(m_inputImageTextureHandle, 0);
        glUniform1f(m_locThreshold, threshold);
        glUniform1f(m_locKnee, knee);
        drawQuad();
        threshFbo->unbind();
    }

    // --- Passes 2..N+1: Kawase blur on threshold result ---
    {
        GLStateManager::getInstance().useProgram(m_kawaseProg);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(m_locKawaseInputTex, 0);
        glUniform2f(m_locKawaseTexelSize, 1.0f / W, 1.0f / H);

        GLuint srcTex = threshFbo->getTexture().id;

        for (int i = 0; i < iterations; ++i) {
            FrameBufferPtr dst = (i % 2 == 0) ? blurFboA : blurFboB;
            dst->bind();
            glViewport(0, 0, W, H);
            GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, srcTex);
            glUniform1f(m_locKawaseOffset, (i + 0.5f));
            drawQuad();
            dst->unbind();
            srcTex = dst->getTexture().id;
        }

        // blurred result is in blurFboA (even iterations) or blurFboB (odd)
        GLuint blurredTex = ((iterations % 2 == 0) ? blurFboB : blurFboA)->getTexture().id;

        // --- Pass N+2: Additive composite ---
        outputFb->bind();
        glViewport(0, 0, W, H);
        GLStateManager::getInstance().useProgram(m_compProg);

        glActiveTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
        glUniform1i(m_locCompInputTex, 0);

        glActiveTexture(GL_TEXTURE1);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, blurredTex);
        glUniform1i(m_locBloomTex, 1);

        glUniform1f(m_locIntensity, intensity);
        drawQuad();
        outputFb->unbind();

        glActiveTexture(GL_TEXTURE0);
    }

    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

} // namespace video
} // namespace sdk
