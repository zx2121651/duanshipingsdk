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
    FrameBuffer(int width, int height);
    ~FrameBuffer();

    // Non-copyable
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    void bind();
    void unbind();

    Texture getTexture() const { return {m_textureId, m_width, m_height}; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    GLuint m_fboId;
    GLuint m_textureId;
    int m_width;
    int m_height;
};

using FrameBufferPtr = std::shared_ptr<FrameBuffer>;

} // namespace video
} // namespace sdk
