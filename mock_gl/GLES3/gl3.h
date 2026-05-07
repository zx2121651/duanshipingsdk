
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
#define GL_INVALID_INDEX 0xFFFFFFFFu
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
#define GL_INVALID_INDEX 0xFFFFFFFFu
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

// For GLContextManager.cpp
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
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
inline void glUniform2fv(GLint, GLsizei, const GLfloat*) {}
inline void glUniform3fv(GLint, GLsizei, const GLfloat*) {}
inline void glUniform4fv(GLint, GLsizei, const GLfloat*) {}
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
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
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
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif

// --- Texture format constants missing from base mock ---
#ifndef GL_R8
#define GL_R8      0x8229
#endif
#ifndef GL_RED
#define GL_RED     0x1903
#endif
#ifndef GL_RG8
#define GL_RG8     0x822B
#endif
#ifndef GL_RG
#define GL_RG      0x8227
#endif
#ifndef GL_RG16F
#define GL_RG16F   0x822F
#endif
#ifndef GL_RGB8
#define GL_RGB8    0x8051
#endif
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT   0x1902
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

// --- Blend / state constants ---
#ifndef GL_BLEND
#define GL_BLEND         0x0BE2
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE     0x0B44
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST    0x0B71
#endif
#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA           0x0302
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif
#ifndef GL_SRC_COLOR
#define GL_SRC_COLOR           0x0300
#endif
#ifndef GL_ONE_MINUS_SRC_COLOR
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#endif
#ifndef GL_DST_COLOR
#define GL_DST_COLOR           0x0306
#endif
#ifndef GL_ONE_MINUS_DST_COLOR
#define GL_ONE_MINUS_DST_COLOR 0x0307
#endif
#ifndef GL_DST_ALPHA
#define GL_DST_ALPHA           0x0304
#endif
#ifndef GL_ONE_MINUS_DST_ALPHA
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#endif
#ifndef GL_CONSTANT_ALPHA
#define GL_CONSTANT_ALPHA           0x8003
#endif
#ifndef GL_ONE_MINUS_CONSTANT_ALPHA
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif
#ifndef GL_CONSTANT_COLOR
#define GL_CONSTANT_COLOR           0x8001
#endif
#ifndef GL_ONE_MINUS_CONSTANT_COLOR
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#endif
#ifndef GL_SRC_ALPHA_SATURATE
#define GL_SRC_ALPHA_SATURATE  0x0308
#endif
#ifndef GL_ONE
#define GL_ONE  1
#endif
#ifndef GL_ZERO
#define GL_ZERO 0
#endif
#ifndef GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS 0x90EB
#endif
#ifndef GL_BACK
#define GL_BACK  0x0405
#endif
#ifndef GL_FRONT
#define GL_FRONT 0x0404
#endif

// --- Additional GL function stubs ---
#ifndef GL_MOCK_EXTENDED
#define GL_MOCK_EXTENDED

#ifndef GLbitfield
typedef unsigned int GLbitfield;
#endif

inline void  glFlush() {}
inline void  glUniform2f(GLint, GLfloat, GLfloat) {}
inline void  glUniform4f(GLint, GLfloat, GLfloat, GLfloat, GLfloat) {}
inline void  glUniform3f(GLint, GLfloat, GLfloat, GLfloat) {}
inline void  glUniform2i(GLint, GLint, GLint) {}
inline void  glBlendFunc(GLenum, GLenum) {}
inline void  glBlendFuncSeparate(GLenum, GLenum, GLenum, GLenum) {}
inline void  glDepthMask(GLboolean) {}
inline void  glCullFace(GLenum) {}
inline void  glColorMask(GLboolean, GLboolean, GLboolean, GLboolean) {}
inline void  glScissor(GLint, GLint, GLsizei, GLsizei) {}
inline GLenum glGetError() { return 0; }
inline void  glDetachShader(GLuint, GLuint) {}
inline void* glMapBufferRange(GLenum, GLintptr, GLsizeiptr, GLbitfield) { return nullptr; }
inline GLboolean glUnmapBuffer(GLenum) { return GL_TRUE; }

// --- 3D Texture support (GLES 3.0) ---
#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
inline void glTexImage3D(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {}
inline void glTexSubImage3D(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*) {}
inline void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) {}
inline void glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) {}
inline void glPixelStorei(GLenum, GLint) {}
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
// GLES 3.2 shader stages
#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER          0x8DD9
#endif
#ifndef GL_TESS_CONTROL_SHADER
#define GL_TESS_CONTROL_SHADER      0x8E88
#endif
#ifndef GL_TESS_EVALUATION_SHADER
#define GL_TESS_EVALUATION_SHADER   0x8E87
#endif
// GLES 3.2 multisample texture
#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE   0x9100
#endif
inline void glTexStorage2DMultisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) {}
inline void glRenderbufferStorageMultisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei) {}

#endif // GL_MOCK_EXTENDED

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif
