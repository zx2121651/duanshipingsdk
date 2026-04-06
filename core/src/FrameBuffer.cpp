#include "../include/FrameBuffer.h"
#include <iostream>

namespace sdk {
namespace video {

FrameBuffer::FrameBuffer(int width, int height, FBOPrecision precision)
    : m_width(width), m_height(height), m_precision(precision), m_ownsFbo(true) {

    glGenFramebuffers(1, &m_fboId);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fboId);

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    GLint internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;

    switch (m_precision) {
        case FBOPrecision::FP16:
            internalFormat = GL_RGBA16F;
            type = GL_HALF_FLOAT;
            break;
        case FBOPrecision::RGB565:
            internalFormat = GL_RGB565;
            format = GL_RGB;
            type = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case FBOPrecision::RGBA8888:
        default:
            break;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_width, m_height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textureId, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "FrameBuffer incomplete: " << status << std::endl;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

FrameBuffer::FrameBuffer(int width, int height, GLuint externalFboId)
    : m_width(width), m_height(height), m_precision(FBOPrecision::RGBA8888),
      m_fboId(externalFboId), m_textureId(0), m_ownsFbo(false) {
    // 外部提供的 FBO，本类只负责包装以适配 Compositor 渲染管线，不负责纹理分配与最终销毁。
}

FrameBuffer::~FrameBuffer() {
    if (m_ownsFbo) {
        if (m_textureId != 0) glDeleteTextures(1, &m_textureId);
        if (m_fboId != 0) glDeleteFramebuffers(1, &m_fboId);
    }
}

void FrameBuffer::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fboId);
    glViewport(0, 0, m_width, m_height);
}

void FrameBuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace video
} // namespace sdk
