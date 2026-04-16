#include "../../include/FilterEngine.h"
#include "../../include/timeline/Compositor.h"
#include "../../include/GLStateManager.h"
#include <iostream>

namespace sdk {
namespace video {
namespace timeline {

Compositor::Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine)
    : m_timeline(timeline), m_filterEngine(engine), m_decoderPool(nullptr) {}

Result Compositor::initPrograms() {
    Result res = initCopyProgram();
    if (!res.isOk()) return res;
    res = initBlendProgram();
    if (!res.isOk()) return res;
    res = initWipeTransitionProgram();
    if (!res.isOk()) return res;
    return Result::ok();
}

static GLuint compileShader(GLenum type, const char* s) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &s, NULL);
    glCompileShader(sh);
    GLint status;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char buf[512];
        glGetShaderInfoLog(sh, 512, NULL, buf);
        std::cerr << "Compositor Shader Compile Error: " << buf << std::endl;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static Result linkProgram(GLuint program) {
    glLinkProgram(program);
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        char buf[512];
        glGetProgramInfoLog(program, 512, NULL, buf);
        std::cerr << "Compositor Program Link Error: " << buf << std::endl;
        return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "Program link failed");
    }
    return Result::ok();
}

Result Compositor::initCopyProgram() {
    if (m_copyProgram != 0) return Result::ok();
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
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsrc);
    if (!vs) return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "Vertex shader compile failed");
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsrc);
    if (!fs) { glDeleteShader(vs); return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "Fragment shader compile failed"); }

    m_copyProgram = glCreateProgram();
    glAttachShader(m_copyProgram, vs);
    glAttachShader(m_copyProgram, fs);
    Result res = linkProgram(m_copyProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!res.isOk()) {
        glDeleteProgram(m_copyProgram);
        m_copyProgram = 0;
    }
    return res;
}

Result Compositor::copyTexture(const Texture& src, FrameBufferPtr target, float opacity) {
    if (!target) return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Target FBO is null");
    if (src.id == 0) return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Source texture ID is 0");

    target->bind();
    GLStateManager::getInstance().useProgram(m_copyProgram);
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, src.id);
    glUniform1i(glGetUniformLocation(m_copyProgram, "texForeground"), 0);
    glUniform1f(glGetUniformLocation(m_copyProgram, "opacity"), opacity);

    static const float squareCoords[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const float textureCoords[] = {0, 0, 1, 0, 0, 1, 1, 1};
    GLStateManager::getInstance().enableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, squareCoords);
    GLStateManager::getInstance().enableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    target->unbind();
    return Result::ok();
}

Result Compositor::initBlendProgram() {
    if (m_blendProgram != 0) return Result::ok();

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

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsrc);
    if (!vs) return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "Vertex shader compile failed");
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsrc);
    if (!fs) { glDeleteShader(vs); return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "Fragment shader compile failed"); }

    m_blendProgram = glCreateProgram();
    glAttachShader(m_blendProgram, vs);
    glAttachShader(m_blendProgram, fs);
    Result res = linkProgram(m_blendProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!res.isOk()) {
        glDeleteProgram(m_blendProgram);
        m_blendProgram = 0;
    }
    return res;
}

Result Compositor::initWipeTransitionProgram() {
    if (m_wipeTransitionProgram != 0) return Result::ok();

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

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsrc);
    if (!vs) return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "Vertex shader compile failed");
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsrc);
    if (!fs) { glDeleteShader(vs); return Result::error(ErrorCode::ERR_TIMELINE_COMPOSITOR_INIT_FAILED, "Fragment shader compile failed"); }

    m_wipeTransitionProgram = glCreateProgram();
    glAttachShader(m_wipeTransitionProgram, vs);
    glAttachShader(m_wipeTransitionProgram, fs);
    Result res = linkProgram(m_wipeTransitionProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!res.isOk()) {
        glDeleteProgram(m_wipeTransitionProgram);
        m_wipeTransitionProgram = 0;
    }
    return res;
}

ResultPayload<Texture> Compositor::blendTextures(const Texture& bg, const Texture& fg, float opacity, FrameBufferPtr target) {
    if (!target) return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Target FBO is null");

    target->bind();
    GLStateManager::getInstance().useProgram(m_blendProgram);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, bg.id);
    glUniform1i(glGetUniformLocation(m_blendProgram, "texBackground"), 0);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, fg.id);
    glUniform1i(glGetUniformLocation(m_blendProgram, "texForeground"), 1);

    glUniform1f(glGetUniformLocation(m_blendProgram, "opacity"), opacity);

    static const float squareCoords[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const float textureCoords[] = {0, 0, 1, 0, 0, 1, 1, 1};

    GLStateManager::getInstance().enableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, squareCoords);
    GLStateManager::getInstance().enableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    target->unbind();

    return ResultPayload<Texture>::ok(target->getTexture());
}

ResultPayload<Texture> Compositor::transitionTextures(const Texture& bg, const Texture& fg, TransitionType type, float progress, FrameBufferPtr target) {
    if (!target) return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Target FBO is null");

    target->bind();

    GLuint program = m_blendProgram; // fallback
    if (type == TransitionType::CROSSFADE) {
        program = m_blendProgram;
    } else if (type == TransitionType::WIPE_LEFT) {
        program = m_wipeTransitionProgram;
    }

    GLStateManager::getInstance().useProgram(program);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, bg.id);
    glUniform1i(glGetUniformLocation(program, "texBackground"), 0);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, fg.id);
    glUniform1i(glGetUniformLocation(program, "texForeground"), 1);

    if (type == TransitionType::CROSSFADE) {
        glUniform1f(glGetUniformLocation(program, "opacity"), progress);
    } else if (type == TransitionType::WIPE_LEFT) {
        glUniform1f(glGetUniformLocation(program, "progress"), progress);
    }

    static const float squareCoords[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const float textureCoords[] = {0, 0, 1, 0, 0, 1, 1, 1};

    GLStateManager::getInstance().enableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, squareCoords);
    GLStateManager::getInstance().enableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    target->unbind();

    return ResultPayload<Texture>::ok(target->getTexture());
}

Result Compositor::renderFrameAtTime(int64_t timelineNs, FrameBufferPtr outputFb) {
    if (!m_timeline || !m_filterEngine || !outputFb) {
        return Result::error(ErrorCode::ERR_TIMELINE_NULL, "Compositor not properly initialized with Timeline/Engine/FBO");
    }

    Result res = initPrograms();
    if (!res.isOk()) return res;

    m_timeline->getActiveVideoClipsAtTime(timelineNs, m_activeClips);

    if (m_activeClips.empty()) {
        outputFb->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        outputFb->unbind();
        return Result::ok();
    }

    if (!m_decoderPool) {
        return Result::error(ErrorCode::ERR_TIMELINE_DECODER_POOL_NULL, "Active clips exist but no decoder pool set");
    }

    Texture accumulatedTexture = {0, 0, 0};
    bool isFirst = true;

    FrameBufferPtr pingFb = m_filterEngine->getFrameBufferPool()->getFrameBuffer(outputFb->width(), outputFb->height());
    FrameBufferPtr pongFb = m_filterEngine->getFrameBufferPool()->getFrameBuffer(outputFb->width(), outputFb->height());

    if (!pingFb || !pongFb) {
        return Result::error(ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED, "Failed to allocate ping/pong FBOs");
    }

    for (const auto& clip : m_activeClips) {
        int64_t clipRelativeNs = timelineNs - clip->getTimelineIn();
        int64_t localTimeNs = static_cast<int64_t>(clipRelativeNs * clip->getSpeed()) + clip->getEffectiveTrimIn();

        // Clamp local time to effective trim range
        localTimeNs = std::max(clip->getEffectiveTrimIn(), std::min(localTimeNs, clip->getEffectiveTrimOut()));

        ResultPayload<Texture> frameRes = m_decoderPool->getFrame(clip->getId(), localTimeNs);
        if (!frameRes.isOk()) {
            return Result::error(frameRes.getErrorCode(), "Decoder failed for clip " + clip->getId() + ": " + frameRes.getMessage());
        }
        Texture fgTex = frameRes.getValue();
        if (fgTex.id == 0) {
            return Result::error(ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED, "Decoder returned invalid texture for clip: " + clip->getId());
        }

        float alpha = clip->getOpacity(clipRelativeNs);
        TransitionType transitionToUse = TransitionType::NONE;
        float transitionProgress = 1.0f;

        if (clip->getInTransitionType() != TransitionType::NONE && clipRelativeNs < clip->getInTransitionDurationNs()) {
            transitionToUse = clip->getInTransitionType();
            transitionProgress = static_cast<float>(clipRelativeNs) / clip->getInTransitionDurationNs();
        } else if (clip->getOutTransitionType() != TransitionType::NONE) {
            int64_t clipRemainingNs = clip->getTimelineOut() - timelineNs;
            if (clipRemainingNs < clip->getOutTransitionDurationNs()) {
                transitionToUse = clip->getOutTransitionType();
                transitionProgress = static_cast<float>(clipRemainingNs) / clip->getOutTransitionDurationNs();
            }
        }

        if (isFirst) {
            pingFb->bind();
            glClearColor(0.0, 0.0, 0.0, 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
            pingFb->unbind();

            float initialAlpha = alpha;
            if (transitionToUse != TransitionType::NONE) {
                initialAlpha *= transitionProgress;
            }
            res = copyTexture(fgTex, pingFb, initialAlpha);
            if (!res.isOk()) return res;
            accumulatedTexture = pingFb->getTexture();
            isFirst = false;
        } else {
            ResultPayload<Texture> blendRes = ResultPayload<Texture>::error(-1, "");
            if (transitionToUse != TransitionType::NONE) {
                blendRes = transitionTextures(accumulatedTexture, fgTex, transitionToUse, transitionProgress * alpha, pongFb);
            } else {
                blendRes = blendTextures(accumulatedTexture, fgTex, alpha, pongFb);
            }
            if (!blendRes.isOk()) return blendRes;
            accumulatedTexture = blendRes.getValue();
            std::swap(pingFb, pongFb);
        }
    }

    if (accumulatedTexture.id == 0) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Composition produced invalid texture");
    }

    auto processResult = m_filterEngine->processFrame(accumulatedTexture, outputFb->width(), outputFb->height());
    if (!processResult.isOk()) {
        return Result::error(processResult.getErrorCode(), processResult.getMessage());
    }
    Texture finalTex = processResult.getValue();

    return copyTexture(finalTex, outputFb);
}

} // namespace timeline
} // namespace video
} // namespace sdk
