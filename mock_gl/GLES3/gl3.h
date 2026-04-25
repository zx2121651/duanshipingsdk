
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#include <stddef.h>
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef float GLfloat;
typedef char GLchar;
typedef int GLsizei;
typedef unsigned char GLboolean;

#define GL_VERTEX_SHADER 0
#define GL_FRAGMENT_SHADER 1
#define GL_COMPILE_STATUS 2
#define GL_INFO_LOG_LENGTH 3
#define GL_FALSE 0
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_FLOAT 0x1406
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TRUE 1
#define GL_LINK_STATUS 4

// For Filters.cpp
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FLOAT 0x1406

// For FrameBuffer.cpp
#define GL_FRAMEBUFFER 0x8D40
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_RGBA16F 0x881A
#define GL_HALF_FLOAT 0x140B
#define GL_RGB565 0x8D62
#define GL_RGB 0x1907
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

// For GLContextManager.cpp
#define GL_VERSION 0x1F02
#define GL_NUM_EXTENSIONS 0x821D
#define GL_EXTENSIONS 0x1F03
#define GL_COLOR_BUFFER_BIT 0x00004000

// For ShaderManager.cpp
#define GL_COMPUTE_SHADER 0x91B9

inline GLint glGetAttribLocation(GLuint, const char*) { return 0; }
inline GLint glGetUniformLocation(GLuint, const char*) { return 0; }
inline void glDeleteProgram(GLuint) {}
inline GLuint glCreateShader(GLenum) { return 0; }
inline void glShaderSource(GLuint, GLsizei, const GLchar**, const GLint*) {}
inline void glCompileShader(GLuint) {}
inline void glGetShaderiv(GLuint, GLenum, GLint* params) { if (params) *params = GL_TRUE; }
inline void glGetShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*) {}
inline void glDeleteShader(GLuint) {}
inline GLuint glCreateProgram() { return 0; }
inline void glAttachShader(GLuint, GLuint) {}
inline void glLinkProgram(GLuint) {}
inline void glGetProgramiv(GLuint, GLenum, GLint* params) { if (params) *params = GL_TRUE; }
inline void glGetProgramInfoLog(GLuint, GLsizei, GLsizei*, GLchar*) {}

inline void glUseProgram(GLuint) {}
inline void glActiveTexture(GLenum) {}
inline void glBindTexture(GLenum, GLuint) {}
inline void glUniform1f(GLint, GLfloat) {}
inline void glUniform1i(GLint, GLint) {}
inline void glUniformMatrix4fv(GLint, GLsizei, GLboolean, const GLfloat*) {}
inline void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) {}
inline void glEnableVertexAttribArray(GLuint) {}
inline void glBindVertexArray(GLuint) {}
inline void glDrawElements(GLenum, GLsizei, GLenum, const void*) {}
inline void glBindBufferBase(GLenum, GLuint, GLuint) {}
inline void glGenVertexArrays(GLsizei, GLuint*) {}
inline void glDeleteVertexArrays(GLsizei, const GLuint*) {}
inline void glGenBuffers(GLsizei, GLuint*) {}
inline void glDeleteBuffers(GLsizei, const GLuint*) {}
inline void glBindBuffer(GLenum, GLuint) {}
inline void glBufferData(GLenum, GLsizeiptr, const void*, GLenum) {}
inline void glBufferSubData(GLenum, GLintptr, GLsizeiptr, const void*) {}
inline GLuint glGetUniformBlockIndex(GLuint, const char*) { return 0; }
inline void glUniformBlockBinding(GLuint, GLuint, GLuint) {}

inline void glDrawArrays(GLenum, GLint, GLsizei) {}
inline void glDisableVertexAttribArray(GLuint) {}
inline void glEnable(GLenum) {}
inline void glDisable(GLenum) {}

// For FrameBuffer.cpp
inline void glGenFramebuffers(GLsizei n, GLuint* ids) { for(int i=0; i<n; ++i) ids[i] = 100 + i; }
inline void glGenTextures(GLsizei n, GLuint* ids) { for(int i=0; i<n; ++i) ids[i] = 200 + i; }
inline void glTexParameteri(GLenum, GLenum, GLint) {}
inline void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {}
inline void glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint) {}
inline void glDeleteFramebuffers(GLsizei, const GLuint*) {}
inline void glDeleteTextures(GLsizei, const GLuint*) {}
inline GLenum glCheckFramebufferStatus(GLenum) { return GL_FRAMEBUFFER_COMPLETE; }

// For GLStateManager.cpp
inline void glBindFramebuffer(GLenum, GLuint) {}

// For GLContextManager.cpp
inline const unsigned char* glGetString(GLenum) { return nullptr; }
inline const unsigned char* glGetStringi(GLenum, GLuint) { return nullptr; }
inline void glGetIntegerv(GLenum, GLint* params) { if (params) *params = 0; }
inline void glViewport(GLint, GLint, GLsizei, GLsizei) {}
inline void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
inline void glClear(GLuint) {}

// --- RHI Mock Extensions added for Unit Tests ---
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_BYTE
#define GL_BYTE 0x1400
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif
