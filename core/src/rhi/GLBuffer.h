#pragma once
#include "../../include/rhi/IBuffer.h"
#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {
namespace rhi {

class GLBuffer : public IBuffer {
public:
    GLBuffer(BufferType type, BufferUsage usage, size_t size, const void* data = nullptr);
    ~GLBuffer() override;

    BufferType getType() const override { return m_type; }
    size_t getSize() const override { return m_size; }
    void updateData(const void* data, size_t size, size_t offset = 0) override;

    GLuint getGLHandle() const { return m_handle; }

private:
    GLuint m_handle = 0;
    BufferType m_type;
    size_t m_size;
    GLenum m_glTarget;
    GLenum m_glUsage;
};

} // namespace rhi
} // namespace video
} // namespace sdk
