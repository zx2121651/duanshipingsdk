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

class FrameBuffer {
public:
    // 增加 isRgb565 参数，控制纹理位数
    FrameBuffer(int width, int height, bool isRgb565 = false);
    ~FrameBuffer();

    // Non-copyable
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    void bind();
    void unbind();

    Texture getTexture() const { return {m_textureId, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)}; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    bool isRgb565() const { return m_isRgb565; }

private:
    GLuint m_fboId;
    GLuint m_textureId;
    int m_width;
    int m_height;
    bool m_isRgb565;
};

using FrameBufferPtr = std::shared_ptr<FrameBuffer>;

} // namespace video
} // namespace sdk
