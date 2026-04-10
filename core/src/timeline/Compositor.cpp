#include "../../include/FilterEngine.h"
#include "../../include/timeline/Compositor.h"
#include <iostream>

namespace sdk {
namespace video {
namespace timeline {

Compositor::Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine)
    : m_timeline(timeline), m_filterEngine(engine), m_decoderPool(nullptr) {}

void Compositor::initPrograms() {
    initCopyProgram();
    initBlendProgram();
    initWipeTransitionProgram();
}

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

void Compositor::initWipeTransitionProgram() {
    if (m_wipeTransitionProgram != 0) return;

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
        uniform float progress; // 0.0 to 1.0
        out vec4 fragColor;

        void main() {
            vec4 bg = texture(texBackground, v_texCoord);
            vec4 fg = texture(texForeground, v_texCoord);

            // Wipe left transition
            if (v_texCoord.x > (1.0 - progress)) {
                fragColor = fg;
            } else {
                fragColor = bg;
            }
        }
    )";

    auto compile = [](GLenum type, const char* s) {
        GLuint sh = glCreateShader(type); glShaderSource(sh, 1, &s, NULL); glCompileShader(sh); return sh;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
    m_wipeTransitionProgram = glCreateProgram();
    glAttachShader(m_wipeTransitionProgram, vs); glAttachShader(m_wipeTransitionProgram, fs);
    glLinkProgram(m_wipeTransitionProgram);
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

Texture Compositor::transitionTextures(const Texture& bg, const Texture& fg, TransitionType type, float progress, FrameBufferPtr target) {
    if (!target) return bg;

    target->bind();

    GLuint program = m_blendProgram; // fallback
    if (type == TransitionType::CROSSFADE) {
        program = m_blendProgram;
    } else if (type == TransitionType::WIPE_LEFT) {
        program = m_wipeTransitionProgram;
    }

    glUseProgram(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bg.id);
    glUniform1i(glGetUniformLocation(program, "texBackground"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fg.id);
    glUniform1i(glGetUniformLocation(program, "texForeground"), 1);

    if (type == TransitionType::CROSSFADE) {
        glUniform1f(glGetUniformLocation(program, "opacity"), progress);
    } else if (type == TransitionType::WIPE_LEFT) {
        glUniform1f(glGetUniformLocation(program, "progress"), progress);
    }

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

Result Compositor::renderFrameAtTime(int64_t timelineNs, FrameBufferPtr outputFb) {
    if (!m_timeline || !m_filterEngine || !outputFb) {
        return Result::error(-3, "Compositor not properly initialized with Timeline/Engine/FBO");
    }

    initPrograms();

    std::vector<ClipPtr> activeClips = m_timeline->getActiveVideoClipsAtTime(timelineNs);

    if (activeClips.empty() || !m_decoderPool) {
        outputFb->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        outputFb->unbind();
        return Result::ok();
    }

    Texture accumulatedTexture = {0, 0, 0};
    bool isFirst = true;

    FrameBufferPtr pingFb = m_filterEngine->getFrameBufferPool()->getFrameBuffer(outputFb->width(), outputFb->height());
    FrameBufferPtr pongFb = m_filterEngine->getFrameBufferPool()->getFrameBuffer(outputFb->width(), outputFb->height());

    for (const auto& clip : activeClips) {
        int64_t localTimeNs = (timelineNs - clip->getTimelineIn()) * clip->getSpeed() + clip->getTrimIn();

        Texture fgTex = m_decoderPool->getFrame(clip->getId(), localTimeNs);
        if (fgTex.id == 0) continue;

        if (isFirst) {
            pingFb->bind();
            glClearColor(0.0, 0.0, 0.0, 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
            pingFb->unbind();

            copyTexture(fgTex, pingFb);
            accumulatedTexture = pingFb->getTexture();
            isFirst = false;
        } else {
            // Check Transition
            int64_t clipRelativeNs = timelineNs - clip->getTimelineIn();
            if (clip->getInTransitionType() != TransitionType::NONE && clipRelativeNs < clip->getInTransitionDurationNs()) {
                // Inside transition window
                float progress = static_cast<float>(clipRelativeNs) / clip->getInTransitionDurationNs();
                accumulatedTexture = transitionTextures(accumulatedTexture, fgTex, clip->getInTransitionType(), progress, pongFb);
            } else {
                // Keyframe interpolated Alpha Blending
                // Nse keyframe value instead of static value!
                float interpolatedOpacity = clip->getOpacity(clipRelativeNs);
                accumulatedTexture = blendTextures(accumulatedTexture, fgTex, interpolatedOpacity, pongFb);
            }
            std::swap(pingFb, pongFb);
        }
    }

    auto result = m_filterEngine->processFrame(accumulatedTexture, outputFb->width(), outputFb->height());
    if (!result.isOk()) {
        return Result::error(result.getErrorCode(), result.getMessage());
    }
    Texture finalTex = result.getValue();

    copyTexture(finalTex, outputFb);

    return Result::ok();
}

} // namespace timeline
} // namespace video
} // namespace sdk
