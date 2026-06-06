import os

file_path = '/app/sdk-core/core/src/Filters.cpp'
with open(file_path, 'r') as f:
    content = f.read()

# OES2RGB
oes_old = """
void OES2RGBFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();

    // [FIX] Force-breaking GLStateManager's program cache.  Must go
    // through GLStateManager (not raw glUseProgram) so the cache is
    // updated.  Without this, uniforms hit GL_INVALID_OPERATION.
    GLStateManager::getInstance().useProgram(0);
    GLStateManager::getInstance().useProgram(m_programId);
    CHECK_GL_ERROR_LINE();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    CHECK_GL_ERROR_LINE();

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_EXTERNAL_OES, inputTexture.id);
    CHECK_GL_ERROR_LINE();

    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    // [FIX] Verify uniform location before calling glUniform.
    // GLuint is unsigned — do NOT use handle >= 0 (always true).
    if (m_inputImageTextureHandle != static_cast<GLuint>(-1)) {
        glUniform1i(m_inputImageTextureHandle, 0);
    }
    CHECK_GL_ERROR_LINE();

    if (m_mat4Parameters.count("textureMatrix")) {
        auto& matrix = m_mat4Parameters.at("textureMatrix");
        if (m_textureMatrixHandle != static_cast<GLuint>(-1))
            glUniformMatrix4fv(m_textureMatrixHandle, 1, GL_FALSE, matrix.data());
    }

    bool flipH = false;
    if (m_parameters.count("flipHorizontal") && m_parameters.at("flipHorizontal").type() == typeid(bool)) {
        flipH = std::any_cast<bool>(m_parameters.at("flipHorizontal"));
    }
    if (m_flipHorizontalHandle != static_cast<GLuint>(-1))
        glUniform1i(m_flipHorizontalHandle, flipH ? 1 : 0);

    bool flipV = false;
    if (m_parameters.count("flipVertical") && m_parameters.at("flipVertical").type() == typeid(bool)) {
        flipV = std::any_cast<bool>(m_parameters.at("flipVertical"));
    }
    if (m_flipVerticalHandle != static_cast<GLuint>(-1))
        glUniform1i(m_flipVerticalHandle, flipV ? 1 : 0);
    CHECK_GL_ERROR_LINE();

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

    GLStateManager::getInstance().bindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    outputFb->unbind();
}
"""

oes_new = """
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
        if (m_mat4Parameters.count("textureMatrix")) {
            auto& matrix = m_mat4Parameters.at("textureMatrix");
            if (m_programId > 0 && m_textureMatrixHandle != static_cast<GLuint>(-1))
                glUniformMatrix4fv(m_textureMatrixHandle, 1, GL_FALSE, matrix.data());
        }
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
    std::array<float, 16> matrix = m_mat4Parameters["textureMatrix"];
    bool flipH = m_parameters.count("flipHorizontal") ? std::any_cast<bool>(m_parameters.at("flipHorizontal")) : false;
    bool flipV = m_parameters.count("flipVertical") ? std::any_cast<bool>(m_parameters.at("flipVertical")) : false;

    float bufferData[18];
    for (int i = 0; i < 16; i++) bufferData[i] = matrix[i];
    bufferData[16] = flipH ? 1.0f : 0.0f;
    bufferData[17] = flipV ? 1.0f : 0.0f;
    m_paramsBuffer->updateData(bufferData, sizeof(bufferData));

    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    if (!cmdBuffer) return;
    rhi::RenderPassDescriptor passDesc;
    passDesc.colorAttachments[0].texture = std::make_shared<rhi::GLTexture>(outputFb->getTexture().id, outputFb->getTexture().width, outputFb->getTexture().height);
    passDesc.colorAttachments[0].loadAction = rhi::LoadAction::Clear;
    passDesc.colorAttachments[0].clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, m_renderDevice->createTextureFromHardwareBuffer({(void*)(uintptr_t)inputTexture.id, (int)inputTexture.width, (int)inputTexture.height, 0}));
        resourceSet->bindUniformBuffer(1, m_paramsBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }
    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
}
"""
content = content.replace(oes_old, oes_new)

# Brightness
b_old = """
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
"""

b_new = """
void BrightnessFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (!m_renderDevice || m_renderDevice->getCapabilities().backend == rhi::BackendType::GLES) {
        outputFb->bind();
        if (m_programId > 0) {
            GLStateManager::getInstance().useProgram(m_programId);
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);

        float brightness = 0.0f;
        if (m_parameters.count("brightness") && m_parameters.at("brightness").type() == typeid(float)) {
            brightness = std::any_cast<float>(m_parameters.at("brightness"));
        }
        if (m_programId > 0 && m_brightnessHandle != (GLuint)-1) {
            glUniform1f(m_brightnessHandle, brightness);
        }

        if (m_renderDevice && m_quadVao) {
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

    if (!m_pipelineState || !m_brightnessBuffer || !m_quadVao) return;

    float brightness = 0.0f;
    if (m_parameters.count("brightness") && m_parameters.at("brightness").type() == typeid(float)) {
        brightness = std::any_cast<float>(m_parameters.at("brightness"));
    }

    m_brightnessBuffer->updateData(&brightness, sizeof(float));

    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    if (!cmdBuffer) return;

    rhi::RenderPassDescriptor passDesc;
    passDesc.colorAttachments[0].texture = std::make_shared<rhi::GLTexture>(outputFb->getTexture().id, outputFb->getTexture().width, outputFb->getTexture().height);
    passDesc.colorAttachments[0].loadAction = rhi::LoadAction::Clear;
    passDesc.colorAttachments[0].clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, m_renderDevice->createTextureFromHardwareBuffer({(void*)(uintptr_t)inputTexture.id, (int)inputTexture.width, (int)inputTexture.height, 0}));
        resourceSet->bindUniformBuffer(1, m_brightnessBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
}
"""
content = content.replace(b_old, b_new)

# Gaussian
g_old = """
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
    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    glUniform1i(m_inputImageTextureHandle, 0);

    // 设置水平方向的偏移量，垂直方向为 0
    glUniform1f(m_texelWidthOffsetHandle, 1.0f / inputTexture.width);
    glUniform1f(m_texelHeightOffsetHandle, 0.0f);

    float blurSize = 1.0f;
    if (m_parameters.count("blurSize")) {
        try { blurSize = std::any_cast<float>(m_parameters.at("blurSize")); } catch (const std::bad_any_cast& e) { std::cerr << "Parameter type cast error: " << e.what() << std::endl; }
    }
    glUniform1f(m_blurSizeHandle, blurSize);
    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

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
    auto cmdBuffer2 = m_renderDevice->createCommandBuffer();
    cmdBuffer2->bindVertexArray(m_quadVao.get());
    cmdBuffer2->draw(4);


    // 释放临时 FBO，归还到池中
    m_pool->release(intermediateFb);

    return ResultPayload<Texture>::ok(outputFb->getTexture());
}
"""

g_new = """
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

    rhi::RenderPassDescriptor passDescH;
    passDescH.colorAttachments[0].texture = std::make_shared<rhi::GLTexture>(intermediateFb->getTexture().id, intermediateFb->getTexture().width, intermediateFb->getTexture().height);
    passDescH.colorAttachments[0].loadAction = rhi::LoadAction::Clear;
    passDescH.colorAttachments[0].clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    cmdBuffer->beginRenderPass(passDescH);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSetH = m_renderDevice->createShaderResourceSet();
    if (resourceSetH) {
        resourceSetH->bindTexture(0, m_renderDevice->createTextureFromHardwareBuffer({(void*)(uintptr_t)inputTexture.id, (int)inputTexture.width, (int)inputTexture.height, 0}));
        resourceSetH->bindUniformBuffer(1, m_blurParamsBufferH);
        cmdBuffer->bindResourceSet(0, resourceSetH);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();

    cmdBuffer->pipelineBarrier(rhi::BarrierType::Pipeline);

    float paramsV[4] = { 0.0f, 1.0f / inputTexture.height, blurSize, 0.0f };
    m_blurParamsBufferV->updateData(paramsV, sizeof(paramsV));

    rhi::RenderPassDescriptor passDescV;
    passDescV.colorAttachments[0].texture = std::make_shared<rhi::GLTexture>(outputFb->getTexture().id, outputFb->getTexture().width, outputFb->getTexture().height);
    passDescV.colorAttachments[0].loadAction = rhi::LoadAction::Clear;
    passDescV.colorAttachments[0].clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

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
"""
content = content.replace(g_old, g_new)

# Lookup
l_old = """
void LookupFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    auto cmdBuffer = m_renderDevice->createCommandBuffer();
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

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}
"""

l_new = """
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

    rhi::RenderPassDescriptor passDesc;
    passDesc.colorAttachments[0].texture = std::make_shared<rhi::GLTexture>(outputFb->getTexture().id, outputFb->getTexture().width, outputFb->getTexture().height);
    passDesc.colorAttachments[0].loadAction = rhi::LoadAction::Clear;
    passDesc.colorAttachments[0].clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, m_renderDevice->createTextureFromHardwareBuffer({(void*)(uintptr_t)inputTexture.id, (int)inputTexture.width, (int)inputTexture.height, 0}));
        if (m_lookupTextureId) {
            resourceSet->bindTexture(1, m_renderDevice->createTextureFromHardwareBuffer({(void*)(uintptr_t)m_lookupTextureId, 512, 512, 0}));
        }
        resourceSet->bindUniformBuffer(2, m_intensityBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
}
"""
content = content.replace(l_old, l_new)


# Bilateral
bi_old = """
void BilateralFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    auto cmdBuffer = m_renderDevice->createCommandBuffer();
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

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}
"""

bi_new = """
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

    rhi::RenderPassDescriptor passDesc;
    passDesc.colorAttachments[0].texture = std::make_shared<rhi::GLTexture>(outputFb->getTexture().id, outputFb->getTexture().width, outputFb->getTexture().height);
    passDesc.colorAttachments[0].loadAction = rhi::LoadAction::Clear;
    passDesc.colorAttachments[0].clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, m_renderDevice->createTextureFromHardwareBuffer({(void*)(uintptr_t)inputTexture.id, (int)inputTexture.width, (int)inputTexture.height, 0}));
        resourceSet->bindUniformBuffer(1, m_paramsBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);
    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
}
"""

content = content.replace(bi_old, bi_new)

# Cinematic Lookup
c_old = """
void CinematicLookupFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    auto cmdBuffer = m_renderDevice->createCommandBuffer();
    glUniform1i(m_inputImageTextureHandle, 0);

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

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);

    outputFb->unbind();
}
"""

c_new = """
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

    rhi::RenderPassDescriptor passDesc;
    passDesc.colorAttachments[0].texture = std::make_shared<rhi::GLTexture>(outputFb->getTexture().id, outputFb->getTexture().width, outputFb->getTexture().height);
    passDesc.colorAttachments[0].loadAction = rhi::LoadAction::Clear;
    passDesc.colorAttachments[0].clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    cmdBuffer->beginRenderPass(passDesc);
    cmdBuffer->bindPipelineState(m_pipelineState);

    auto resourceSet = m_renderDevice->createShaderResourceSet();
    if (resourceSet) {
        resourceSet->bindTexture(0, m_renderDevice->createTextureFromHardwareBuffer({(void*)(uintptr_t)inputTexture.id, (int)inputTexture.width, (int)inputTexture.height, 0}));

        if (m_lookupTextureId != 0) {
            resourceSet->bindTexture(1, m_renderDevice->createTextureFromHardwareBuffer({(void*)(uintptr_t)m_lookupTextureId, 512, 512, 0}));
        }
        resourceSet->bindUniformBuffer(2, m_intensityBuffer);
        cmdBuffer->bindResourceSet(0, resourceSet);
    }

    cmdBuffer->bindVertexArray(m_quadVao.get());
    cmdBuffer->draw(4);

    cmdBuffer->endRenderPass();
    m_renderDevice->submit(cmdBuffer.get());
}
"""

content = content.replace(c_old, c_new)

# Add missing init lines for new RHI objects
init_replace = """Result OES2RGBFilter::initialize() {
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
}"""
content = re.sub(r'Result OES2RGBFilter::initialize\(\) \{.*?return Result::ok\(\);\n\}', init_replace, content, flags=re.DOTALL)

init_replace2 = """Result GaussianBlurFilter::initialize() {
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
}"""
content = re.sub(r'Result GaussianBlurFilter::initialize\(\) \{.*?return Result::ok\(\);\n\}', init_replace2, content, flags=re.DOTALL)

init_replace3 = """Result LookupFilter::initialize() {
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
}"""
content = re.sub(r'Result LookupFilter::initialize\(\) \{.*?return Result::ok\(\);\n\}', init_replace3, content, flags=re.DOTALL)

init_replace4 = """Result BilateralFilter::initialize() {
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
}"""
content = re.sub(r'Result BilateralFilter::initialize\(\) \{.*?return Result::ok\(\);\n\}', init_replace4, content, flags=re.DOTALL)


init_replace5 = """Result CinematicLookupFilter::initialize() {
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
}"""
content = re.sub(r'Result CinematicLookupFilter::initialize\(\) \{.*?return Result::ok\(\);\n\}', init_replace5, content, flags=re.DOTALL)

with open(file_path, 'w') as f:
    f.write(content)

file_path_h = '/app/sdk-core/core/include/Filters.h'
with open(file_path_h, 'r') as f:
    content_h = f.read()

# Add missing members to headers
def inject_members(cls_name, members, content):
    search_str = f"class {cls_name} : public Filter {{"
    start = content.find(search_str)
    if start == -1: return content
    end = content.find("};", start)

    cls_block = content[start:end]
    if 'm_pipelineState' not in cls_block:
        cls_block = cls_block.replace("private:", "private:\n" + members)
        content = content[:start] + cls_block + content[end:]
    return content

content_h = inject_members('OES2RGBFilter', '    std::shared_ptr<rhi::IPipelineState> m_pipelineState;\n    std::shared_ptr<rhi::IBuffer> m_paramsBuffer;\n', content_h)
content_h = inject_members('GaussianBlurFilter', '    std::shared_ptr<rhi::IPipelineState> m_pipelineState;\n    std::shared_ptr<rhi::IBuffer> m_blurParamsBufferH;\n    std::shared_ptr<rhi::IBuffer> m_blurParamsBufferV;\n', content_h)
content_h = inject_members('LookupFilter', '    std::shared_ptr<rhi::IPipelineState> m_pipelineState;\n    std::shared_ptr<rhi::IBuffer> m_intensityBuffer;\n', content_h)
content_h = inject_members('BilateralFilter', '    std::shared_ptr<rhi::IPipelineState> m_pipelineState;\n    std::shared_ptr<rhi::IBuffer> m_paramsBuffer;\n', content_h)
content_h = inject_members('CinematicLookupFilter', '    std::shared_ptr<rhi::IPipelineState> m_pipelineState;\n    std::shared_ptr<rhi::IBuffer> m_intensityBuffer;\n', content_h)

with open(file_path_h, 'w') as f:
    f.write(content_h)
