#include "../../include/timeline/Compositor.h"
#include <iostream>

namespace sdk {
namespace video {
namespace timeline {

Compositor::Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine)
    : m_timeline(timeline), m_filterEngine(engine), m_decoderPool(nullptr) {}

void Compositor::initCopyProgram() {
    if (m_copyProgram != 0) return;
    const char* vsrc = R"(#version 300 es
        layout(location = 0) in vec4 position;
        layout(location = 1) in vec2 texCoord;
        out vec2 v_texCoord;
        void main() {
            gl_Position = position;
            v_texCoord = texCoord;
        }
    )";
    const char* fsrc = R"(#version 300 es
        precision highp float;
        in vec2 v_texCoord;
        uniform sampler2D texForeground;
        uniform float opacity;
        out vec4 fragColor;
        void main() {
            vec4 fg = texture(texForeground, v_texCoord);
            fragColor = vec4(fg.rgb, fg.a * opacity);
        }
    )";
    auto compile = [](GLenum type, const char* s) {
        GLuint sh = glCreateShader(type); glShaderSource(sh, 1, &s, NULL); glCompileShader(sh); return sh;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
    m_copyProgram = glCreateProgram();
    glAttachShader(m_copyProgram, vs); glAttachShader(m_copyProgram, fs);
    glLinkProgram(m_copyProgram);
    glDeleteShader(vs); glDeleteShader(fs);
}

void Compositor::copyTexture(const Texture& src, FrameBufferPtr target) {
    if (!target || src.id == 0) return;
    target->bind();
    glUseProgram(m_copyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src.id);
    glUniform1i(glGetUniformLocation(m_copyProgram, "texForeground"), 0);
    glUniform1f(glGetUniformLocation(m_copyProgram, "opacity"), 1.0f);

    static const float squareCoords[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const float textureCoords[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, squareCoords);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    target->unbind();
}

void Compositor::initBlendProgram() {
    if (m_blendProgram != 0) return;

    const char* vsrc = R"(#version 300 es
        layout(location = 0) in vec4 position;
        layout(location = 1) in vec2 texCoord;
        out vec2 v_texCoord;
        void main() {
            gl_Position = position;
            v_texCoord = texCoord;
        }
    )";

    const char* fsrc = R"(#version 300 es
        precision highp float;
        in vec2 v_texCoord;
        uniform sampler2D texBackground;
        uniform sampler2D texForeground;
        uniform float opacity;
        out vec4 fragColor;

        void main() {
            vec4 bg = texture(texBackground, v_texCoord);
            vec4 fg = texture(texForeground, v_texCoord);
            // Alpha Blending (src_alpha, one_minus_src_alpha)
            vec4 blended = fg * fg.a * opacity + bg * (1.0 - fg.a * opacity);
            fragColor = vec4(blended.rgb, max(bg.a, fg.a * opacity));
        }
    )";

    auto compile = [](GLenum type, const char* s) {
        GLuint sh = glCreateShader(type); glShaderSource(sh, 1, &s, NULL); glCompileShader(sh); return sh;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
    m_blendProgram = glCreateProgram();
    glAttachShader(m_blendProgram, vs); glAttachShader(m_blendProgram, fs);
    glLinkProgram(m_blendProgram);
    glDeleteShader(vs); glDeleteShader(fs);
}

Texture Compositor::blendTextures(const Texture& bg, const Texture& fg, float opacity, FrameBufferPtr target) {
    if (!target) return bg;

    target->bind();
    glUseProgram(m_blendProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bg.id);
    glUniform1i(glGetUniformLocation(m_blendProgram, "texBackground"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fg.id);
    glUniform1i(glGetUniformLocation(m_blendProgram, "texForeground"), 1);

    glUniform1f(glGetUniformLocation(m_blendProgram, "opacity"), opacity);

    static const float squareCoords[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const float textureCoords[] = {0, 0, 1, 0, 0, 1, 1, 1};

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, squareCoords);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    target->unbind();

    return target->getTexture();
}

Result Compositor::renderFrameAtTime(int64_t timelineUs, FrameBufferPtr outputFb) {
    if (!m_timeline || !m_filterEngine || !outputFb) {
        return Result::error(-3, "Compositor not properly initialized with Timeline/Engine/FBO");
    }

    initBlendProgram();
    initCopyProgram();

    std::vector<ClipPtr> activeClips = m_timeline->getActiveVideoClipsAtTime(timelineUs);

    if (activeClips.empty() || !m_decoderPool) {
        outputFb->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        outputFb->unbind();
        return Result::ok(); // Blank screen is valid
    }

    Texture accumulatedTexture = {0, 0, 0};
    bool isFirst = true;

    FrameBufferPtr pingFb = m_filterEngine->m_frameBufferPool.getFrameBuffer(outputFb->width(), outputFb->height());
    FrameBufferPtr pongFb = m_filterEngine->m_frameBufferPool.getFrameBuffer(outputFb->width(), outputFb->height());

    for (const auto& clip : activeClips) {
        // A. 变速与裁剪后相对时间的换算
        int64_t localTimeUs = (timelineUs - clip->getTimelineIn()) * clip->getSpeed() + clip->getTrimIn();

        // B. 请求 Decoder
        Texture fgTex = m_decoderPool->getFrame(clip->getId(), localTimeUs);
        if (fgTex.id == 0) continue; // Decoder failed or EOF

        // C. 叠加合成
        if (isFirst) {
            // [P0 修复] 最底下一层，绝对不能与空纹理 {0,0,0} 混合，否则引发 GL 报错
            // 直接使用专用的 copyProgram 画到 pingFb 上作为后续的基底
            pingFb->bind();
            glClearColor(0.0, 0.0, 0.0, 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
            pingFb->unbind(); // bind in copyTexture

            copyTexture(fgTex, pingFb);
            accumulatedTexture = pingFb->getTexture();
            isFirst = false;
        } else {
            // 与已有画面进行 Alpha Blending 画到 pongFb
            accumulatedTexture = blendTextures(accumulatedTexture, fgTex, clip->getOpacity(), pongFb);
            std::swap(pingFb, pongFb); // 交换供下一层使用
        }
    }

    // D. 最后的合并帧还要经过 FilterEngine 做全局特效 (例如套 LUT)
    Texture finalTex = m_filterEngine->processFrame(accumulatedTexture, outputFb->width(), outputFb->height());

    // E. 拷贝到输出目标 outputFb
    // 这里因为 processFrame 返回了最终纹理，我们需要把它画到 outputFb
    outputFb->bind();
    glUseProgram(m_blendProgram); // Reuse blend program as copy with 1.0 opacity and no background
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, finalTex.id);
    glUniform1i(glGetUniformLocation(m_blendProgram, "texForeground"), 1);
    glUniform1f(glGetUniformLocation(m_blendProgram, "opacity"), 1.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    outputFb->unbind();

    return Result::ok();
}

} // namespace timeline
} // namespace video
} // namespace sdk
