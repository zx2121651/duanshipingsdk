#pragma once
#include "GLTypes.h"
#include <memory>

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {

// FBO 的精度级别，根据设备能力动态降级
enum class FBOPrecision {
    FP16,     // 16-bit 浮点 (GL_RGBA16F) - 用于 HDR 电影级调色，防止色阶断层
    RGBA8888, // 32-bit (GL_RGBA8) - 兼容机型的默认格式
    RGB565    // 16-bit (GL_RGB565) - 极致带宽砍半，省电不发热
};

class FrameBuffer {
public:
    FrameBuffer(int width, int height, FBOPrecision precision = FBOPrecision::RGBA8888);

    // 允许显式包装外部 FBO (例如 iOS 的 CVPixelBuffer FBO，或者 Android 的 EGLSurface 默认 FBO 0)
    // 这种情况下，FrameBuffer 对象不会负责销毁这个 m_fboId。
    FrameBuffer(int width, int height, GLuint externalFboId);

    ~FrameBuffer();

    // Non-copyable
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    void bind();
    void unbind();

    Texture getTexture() const { return {m_textureId, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)}; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    GLuint getFboId() const { return m_fboId; }
    FBOPrecision precision() const { return m_precision; }

private:
    GLuint m_fboId;
    GLuint m_textureId;
    int m_width;
    int m_height;
    FBOPrecision m_precision;
    bool m_ownsFbo; // 标识是否拥有所有权，防止误删外部 FBO
};

using FrameBufferPtr = std::shared_ptr<FrameBuffer>;

} // namespace video
} // namespace sdk
