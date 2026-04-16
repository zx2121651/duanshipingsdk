#ifndef MOCK_GL3_H
#define MOCK_GL3_H

#include <cstdint>

typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef float GLfloat;
typedef char GLchar;
typedef int GLsizei;
typedef std::uintptr_t GLsizeiptr;
typedef std::intptr_t GLintptr;
typedef std::uint16_t GLushort;
typedef unsigned char GLboolean;
typedef std::uint32_t GLbitfield;

#define GL_TRUE 1
#define GL_FALSE 0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_2D 0x0DE1
#define GL_FLOAT 0x1406
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FRAMEBUFFER 0x8D40
#define GL_RGBA8 0x8058
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA16F 0x881A
#define GL_HALF_FLOAT 0x140B
#define GL_RGB565 0x8D62
#define GL_RGB 0x1907
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_VERSION 0x1F02
#define GL_NUM_EXTENSIONS 0x821D
#define GL_EXTENSIONS 0x1F03
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

// Mock functions
inline GLuint glCreateShader(GLenum type) { return 1; }
inline void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {}
inline void glCompileShader(GLuint shader) {}
inline void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) { if (pname == GL_COMPILE_STATUS) *params = GL_TRUE; }
inline void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {}
inline void glDeleteShader(GLuint shader) {}
inline GLuint glCreateProgram() { return 1; }
inline void glAttachShader(GLuint program, GLuint shader) {}
inline void glLinkProgram(GLuint program) {}
inline void glGetProgramiv(GLuint program, GLenum pname, GLint* params) { if (pname == GL_LINK_STATUS) *params = GL_TRUE; }
inline void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {}
inline void glDeleteProgram(GLuint program) {}
inline GLint glGetAttribLocation(GLuint program, const GLchar* name) { return 0; }
inline GLint glGetUniformLocation(GLuint program, const GLchar* name) { return 0; }
inline void glUseProgram(GLuint program) {}
inline void glUniform1i(GLint location, GLint v0) {}
inline void glUniform1f(GLint location, GLfloat v0) {}
inline void glActiveTexture(GLenum texture) {}
inline void glBindTexture(GLenum target, GLuint texture) {}
inline void glEnableVertexAttribArray(GLuint index) {}
inline void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {}
inline void glDrawArrays(GLenum mode, GLint first, GLsizei count) {}
inline void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {}
inline void glClear(GLbitfield mask) {}
inline void glGenFramebuffers(GLsizei n, GLuint* framebuffers) { for(int i=0; i<n; ++i) framebuffers[i] = 1; }
inline void glBindFramebuffer(GLenum target, GLuint framebuffer) {}
inline void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {}
inline void glGenTextures(GLsizei n, GLuint* textures) { for(int i=0; i<n; ++i) textures[i] = 1; }
inline void glDeleteTextures(GLsizei n, const GLuint* textures) {}
inline void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels) {}
inline void glTexParameteri(GLenum target, GLenum pname, GLint param) {}
inline void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {}
inline GLenum glCheckFramebufferStatus(GLenum target) { return GL_FRAMEBUFFER_COMPLETE; }
inline void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {}
inline void glDisableVertexAttribArray(GLuint index) {}
inline void glEnable(GLenum cap) {}
inline void glDisable(GLenum cap) {}
inline const char* glGetString(GLenum name) { return "Mock"; }
inline void glGetIntegerv(GLenum pname, GLint* data) { *data = 0; }
inline const char* glGetStringi(GLenum name, GLuint index) { return "Mock"; }
inline void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {}

#endif
