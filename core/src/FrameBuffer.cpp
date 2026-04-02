#include "../include/FrameBuffer.h"

namespace sdk {
namespace video {

FrameBuffer::FrameBuffer(int width, int height, bool isRgb565)
    : m_width(width), m_height(height), m_isRgb565(isRgb565) {
    glGenFramebuffers(1, &m_fboId);
    glGenTextures(1, &m_textureId);

    glBindTexture(GL_TEXTURE_2D, m_textureId);

    // 【核心带宽优化】：如果上层指定这是一个可以牺牲 Alpha 的中间步骤纹理
    // 我们强制采用 GL_RGB + GL_UNSIGNED_SHORT_5_6_5 的格式。
    // 这把单像素的显存占用从 32 bits (RGBA8888) 砍到 16 bits (RGB565)。
    // 带宽压力直接减半！
    if (m_isRgb565) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fboId);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textureId, 0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

FrameBuffer::~FrameBuffer() {
    glDeleteTextures(1, &m_textureId);
    glDeleteFramebuffers(1, &m_fboId);
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
