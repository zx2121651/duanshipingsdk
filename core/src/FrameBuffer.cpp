#include "../include/FrameBuffer.h"

namespace sdk {
namespace video {

FrameBuffer::FrameBuffer(int width, int height, FBOPrecision precision)
    : m_width(width), m_height(height), m_precision(precision) {
    glGenFramebuffers(1, &m_fboId);
    glGenTextures(1, &m_textureId);

    glBindTexture(GL_TEXTURE_2D, m_textureId);

    // 【三级智能降级防线】：根据设备嗅探出的能力决定 FBO 的显存占用和色彩深度
    // 这不仅解决低端机 OOM，更提升了旗舰机的 HDR 画质。
    if (m_precision == FBOPrecision::FP16) {
        // [Tier 3] 旗舰机：启用 16 位浮点 FBO (支持 HDR/Bloom/ToneMapping 高光溢出保护)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    } else if (m_precision == FBOPrecision::RGB565) {
        // [Tier 2/1 中间节点] 省电模式：极致显存带宽优化，比 8888 省一半流量 (4MB vs 8MB)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
    } else {
        // [默认/最后上屏节点] 最安全的传统通道
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
