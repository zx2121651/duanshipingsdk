#include "GLBuffer.h"


#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#define GL_MAP_WRITE_BIT 0x0002
#endif

#ifndef GLbitfield
typedef unsigned int GLbitfield;
#endif

#ifndef __APPLE__
extern "C" {
    void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) __attribute__((weak));
    GLboolean glUnmapBuffer(GLenum target) __attribute__((weak));
}
#endif


#ifndef GLbitfield
typedef unsigned int GLbitfield;
#endif

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

void* GLBuffer::map(size_t offset, size_t size, BufferAccess access) {
    GLbitfield glAccess = 0;
    switch (access) {
        case BufferAccess::Read: glAccess = GL_MAP_READ_BIT; break;
        case BufferAccess::Write: glAccess = GL_MAP_WRITE_BIT; break;
        case BufferAccess::ReadWrite: glAccess = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT; break;
    }

    glBindBuffer(m_glTarget, m_handle);
    void* ptr = glMapBufferRange(m_glTarget, offset, size, glAccess);
    glBindBuffer(m_glTarget, 0);
    return ptr;
}

void GLBuffer::unmap() {
    glBindBuffer(m_glTarget, m_handle);
    glUnmapBuffer(m_glTarget);
    glBindBuffer(m_glTarget, 0);
}

} // namespace rhi
} // namespace video
} // namespace sdk
