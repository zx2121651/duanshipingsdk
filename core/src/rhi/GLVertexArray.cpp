#include "GLVertexArray.h"
#include "GLBuffer.h"

namespace sdk {
namespace video {
namespace rhi {

static GLenum getGLType(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float1:
        case VertexFormat::Float2:
        case VertexFormat::Float3:
        case VertexFormat::Float4:
            return GL_FLOAT;
        case VertexFormat::Byte4:
            return GL_BYTE;
        case VertexFormat::UByte4_Normalized:
            return GL_UNSIGNED_BYTE;
        default:
            return GL_FLOAT;
    }
}

static GLint getGLComponentCount(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float1: return 1;
        case VertexFormat::Float2: return 2;
        case VertexFormat::Float3: return 3;
        case VertexFormat::Float4:
        case VertexFormat::Byte4:
        case VertexFormat::UByte4_Normalized:
            return 4;
        default: return 1;
    }
}

GLVertexArray::GLVertexArray() {
    glGenVertexArrays(1, &m_handle);
}

GLVertexArray::~GLVertexArray() {
    if (m_handle != 0) {
        glDeleteVertexArrays(1, &m_handle);
        m_handle = 0;
    }
}

void GLVertexArray::addVertexBuffer(std::shared_ptr<IBuffer> vertexBuffer, const std::vector<VertexAttribute>& attributes) {
    if (!vertexBuffer || vertexBuffer->getType() != BufferType::VertexBuffer) return;

    m_vertexBuffers.push_back(vertexBuffer);
    auto glBuffer = std::static_pointer_cast<GLBuffer>(vertexBuffer);

    glBindVertexArray(m_handle);
    glBindBuffer(GL_ARRAY_BUFFER, glBuffer->getGLHandle());

    for (const auto& attr : attributes) {
        glEnableVertexAttribArray(attr.location);
        GLboolean normalized = (attr.format == VertexFormat::UByte4_Normalized) ? GL_TRUE : GL_FALSE;
        glVertexAttribPointer(
            attr.location,
            getGLComponentCount(attr.format),
            getGLType(attr.format),
            normalized,
            attr.stride,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset))
        );
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GLVertexArray::setIndexBuffer(std::shared_ptr<IBuffer> indexBuffer) {
    if (!indexBuffer || indexBuffer->getType() != BufferType::IndexBuffer) return;

    m_indexBuffer = indexBuffer;
    auto glBuffer = std::static_pointer_cast<GLBuffer>(indexBuffer);

    glBindVertexArray(m_handle);
    // Binding the element array buffer while the VAO is bound records it in the VAO state
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->getGLHandle());
    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // Restore default state
}

} // namespace rhi
} // namespace video
} // namespace sdk
