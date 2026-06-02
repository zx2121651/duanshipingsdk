#pragma once
#include "../../include/rhi/IVertexArray.h"
#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {
namespace rhi {

class GLVertexArray : public IVertexArray {
public:
    GLVertexArray();
    ~GLVertexArray() override;

    void addVertexBuffer(std::shared_ptr<IBuffer> vertexBuffer, const std::vector<VertexAttribute>& attributes) override;
    void setIndexBuffer(std::shared_ptr<IBuffer> indexBuffer) override;

    GLuint getGLHandle() const { return m_handle; }

private:
    GLuint m_handle = 0;
    std::vector<std::shared_ptr<IBuffer>> m_vertexBuffers;
    std::shared_ptr<IBuffer> m_indexBuffer;
};

} // namespace rhi
} // namespace video
} // namespace sdk
