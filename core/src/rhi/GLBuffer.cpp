#include "GLBuffer.h"

namespace sdk {
namespace video {
namespace rhi {

static GLenum getGLTarget(BufferType type) {
    switch (type) {
        case BufferType::VertexBuffer: return GL_ARRAY_BUFFER;
        case BufferType::IndexBuffer: return GL_ELEMENT_ARRAY_BUFFER;
        case BufferType::UniformBuffer: return GL_UNIFORM_BUFFER;
        default: return GL_ARRAY_BUFFER;
    }
}

static GLenum getGLUsage(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::StaticDraw: return GL_STATIC_DRAW;
        case BufferUsage::DynamicDraw: return GL_DYNAMIC_DRAW;
        case BufferUsage::StreamDraw: return GL_STREAM_DRAW;
        default: return GL_STATIC_DRAW;
    }
}

GLBuffer::GLBuffer(BufferType type, BufferUsage usage, size_t size, const void* data)
    : m_type(type), m_size(size) {
    m_glTarget = getGLTarget(type);
    m_glUsage = getGLUsage(usage);

    glGenBuffers(1, &m_handle);
    glBindBuffer(m_glTarget, m_handle);
    glBufferData(m_glTarget, size, data, m_glUsage);
    glBindBuffer(m_glTarget, 0);
}

GLBuffer::~GLBuffer() {
    if (m_handle != 0) {
        glDeleteBuffers(1, &m_handle);
        m_handle = 0;
    }
}

void GLBuffer::updateData(const void* data, size_t size, size_t offset) {
    if (!data || size == 0) return;

    glBindBuffer(m_glTarget, m_handle);
    if (offset == 0 && size == m_size) {
        // Orphan the old buffer to avoid pipeline stalls
        glBufferData(m_glTarget, size, nullptr, m_glUsage);
        glBufferData(m_glTarget, size, data, m_glUsage);
    } else {
        glBufferSubData(m_glTarget, offset, size, data);
    }
    glBindBuffer(m_glTarget, 0);
}

} // namespace rhi
} // namespace video
} // namespace sdk
