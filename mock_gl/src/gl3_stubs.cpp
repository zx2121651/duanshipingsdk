
#include "../GLES3/gl3.h"

extern "C" {
    void glDetachShader(GLuint program, GLuint shader) {}
    void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) { return nullptr; }
    GLboolean glUnmapBuffer(GLenum target) { return GL_TRUE; }
}
