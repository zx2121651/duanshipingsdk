#include "../include/GLStateManager.h"

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

namespace sdk {
namespace video {

thread_local GLStateManager s_stateManager;

GLStateManager& GLStateManager::getInstance() {
    return s_stateManager;
}

void GLStateManager::invalidateCache() {
    m_currentProgram = 0;
    m_currentFramebuffer = 0;
    m_activeTextureUnit = GL_TEXTURE0;

    for (int i = 0; i < 32; ++i) {
        m_boundTexture2D[i] = 0;
#ifdef __ANDROID__
        m_boundTextureOES[i] = 0;
#endif
    }

    for (int i = 0; i < 16; ++i) {
        m_vertexAttribArrayEnabled[i] = false;
    }
}

void GLStateManager::useProgram(GLuint program) {
    if (m_currentProgram != program || program == 0) {
        glUseProgram(program);
        m_currentProgram = program;
    }
}

void GLStateManager::bindFramebuffer(GLenum target, GLuint framebuffer) {
    if (target == GL_FRAMEBUFFER) {
        if (m_currentFramebuffer != framebuffer || framebuffer == 0) {
            glBindFramebuffer(target, framebuffer);
            m_currentFramebuffer = framebuffer;
        }
    } else {
        glBindFramebuffer(target, framebuffer);
    }
}

void GLStateManager::activeTexture(GLenum textureUnit) {
    if (m_activeTextureUnit != textureUnit) {
        glActiveTexture(textureUnit);
        m_activeTextureUnit = textureUnit;
    }
}

void GLStateManager::bindTexture(GLenum target, GLuint texture) {
    int index = m_activeTextureUnit - GL_TEXTURE0;
    if (index >= 0 && index < 32) {
        if (target == GL_TEXTURE_2D) {
            if (m_boundTexture2D[index] != texture || texture == 0) {
                glBindTexture(target, texture);
                m_boundTexture2D[index] = texture;
            }
        }
#ifdef __ANDROID__
        else if (target == GL_TEXTURE_EXTERNAL_OES) {
            if (m_boundTextureOES[index] != texture || texture == 0) {
                glBindTexture(target, texture);
                m_boundTextureOES[index] = texture;
            }
        }
#endif
        else {
            glBindTexture(target, texture);
        }
    } else {
        glBindTexture(target, texture);
    }
}

void GLStateManager::enableVertexAttribArray(GLuint index) {
    if (index < 16) {
        if (!m_vertexAttribArrayEnabled[index]) {
            glEnableVertexAttribArray(index);
            m_vertexAttribArrayEnabled[index] = true;
        }
    } else {
        glEnableVertexAttribArray(index);
    }
}

void GLStateManager::disableVertexAttribArray(GLuint index) {
    if (index < 16) {
        if (m_vertexAttribArrayEnabled[index]) {
            glDisableVertexAttribArray(index);
            m_vertexAttribArrayEnabled[index] = false;
        }
    } else {
        glDisableVertexAttribArray(index);
    }
}

void GLStateManager::enable(GLenum cap) {
    glEnable(cap);
}

void GLStateManager::disable(GLenum cap) {
    glDisable(cap);
}

} // namespace video
} // namespace sdk
