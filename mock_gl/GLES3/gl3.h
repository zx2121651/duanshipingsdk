
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef float GLfloat;
typedef char GLchar;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPUTE_SHADER 0x91B9
#define GL_GEOMETRY_SHADER 0x8DD9
#define GL_TESS_CONTROL_SHADER 0x8E88
#define GL_TESS_EVALUATION_SHADER 0x8E87
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FALSE 0
#define GL_TRUE 1

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006

#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B

#define GL_RED 0x1903
#define GL_RG 0x8227
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_BGRA_EXT 0x80E1

#define GL_R8 0x8229
#define GL_RG8 0x822B
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_LUMINANCE8 0x8040
#define GL_BGRA8_EXT 0x80E1
#define GL_R16F 0x822D
#define GL_RG16F 0x822F
#define GL_RGBA16F 0x881A
#define GL_R32F 0x822E
#define GL_RGBA32F 0x8814
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT32F 0x8CAC

#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#define GL_COMPRESSED_RGBA_ASTC_6x6_KHR 0x93B4
#define GL_COMPRESSED_RGBA_ASTC_8x8_KHR 0x93B7

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2

#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0

#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_BINDING 0x8CA6

#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_SCISSOR_TEST 0x0C11

#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GL_SRC_ALPHA_SATURATE 0x0308

#define GL_FUNC_ADD 0x8006
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_MIN 0x8007
#define GL_MAX 0x8008

#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_INVERT 0x150A
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508

#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_CCW 0x0901
#define GL_CW 0x0900
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_MAX_SAMPLES 0x8D57

#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400

#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#define GL_ELEMENT_ARRAY_BARRIER_BIT 0x00000002
#define GL_UNIFORM_BARRIER_BIT 0x00000004
#define GL_FRAMEBUFFER_BARRIER_BIT 0x00000400

#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS 0x90EB
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_EXTENSIONS 0x1F03
#define GL_NUM_EXTENSIONS 0x821D
#define GL_INVALID_INDEX 0xFFFFFFFFu

#ifdef __cplusplus
extern "C" {
#endif

// Shader functions
static inline GLuint glCreateShader(GLenum type) { return 1; }
static inline void glShaderSource(GLuint shader, GLsizei count, const GLchar** string, const GLint* length) {}
static inline void glCompileShader(GLuint shader) {}
static inline void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) { if (params) *params = GL_TRUE; }
static inline void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {}
static inline void glDeleteShader(GLuint shader) {}

static inline GLuint glCreateProgram() { return 1; }
static inline void glAttachShader(GLuint program, GLuint shader) {}
static inline void glDetachShader(GLuint program, GLuint shader) {}
static inline void glLinkProgram(GLuint program) {}
static inline void glGetProgramiv(GLuint program, GLenum pname, GLint* params) { if (params) *params = GL_TRUE; }
static inline void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {}
static inline void glUseProgram(GLuint program) {}
static inline void glDeleteProgram(GLuint program) {}

static inline GLint glGetAttribLocation(GLuint program, const GLchar* name) { return 0; }
static inline GLint glGetUniformLocation(GLuint program, const GLchar* name) { return 0; }
static inline void glEnableVertexAttribArray(GLuint index) {}
static inline void glDisableVertexAttribArray(GLuint index) {}
static inline void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {}

static inline void glUniform1f(GLint location, GLfloat v0) {}
static inline void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {}
static inline void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {}
static inline void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {}
static inline void glUniform1i(GLint location, GLint v0) {}
static inline void glUniform2i(GLint location, GLint v0, GLint v1) {}
static inline void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {}
static inline void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {}
static inline void glUniform1fv(GLint location, GLsizei count, const GLfloat* value) {}
static inline void glUniform2fv(GLint location, GLsizei count, const GLfloat* value) {}
static inline void glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {}
static inline void glUniform4fv(GLint location, GLsizei count, const GLfloat* value) {}
static inline void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {}
static inline void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {}

static inline GLuint glGetUniformBlockIndex(GLuint program, const GLchar* uniformBlockName) { return 0; }
static inline void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) {}

// Buffer functions
static inline void glGenBuffers(GLsizei n, GLuint* buffers) { for(int i=0; i<n; ++i) buffers[i] = i+1; }
static inline void glBindBuffer(GLenum target, GLuint buffer) {}
static inline void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {}
static inline void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {}
static inline void glDeleteBuffers(GLsizei n, const GLuint* buffers) {}
static inline void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {}
static inline void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) { return (void*)0x1234; }
static inline GLboolean glUnmapBuffer(GLenum target) { return GL_TRUE; }

// Texture functions
static inline void glGenTextures(GLsizei n, GLuint* textures) { for(int i=0; i<n; ++i) textures[i] = i+1; }
static inline void glBindTexture(GLenum target, GLuint texture) {}
static inline void glActiveTexture(GLenum texture) {}
static inline void glTexParameteri(GLenum target, GLenum pname, GLint param) {}
static inline void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels) {}
static inline void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels) {}
static inline void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void* pixels) {}
static inline void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels) {}
static inline void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void* data) {}
static inline void glGenerateMipmap(GLenum target) {}
static inline void glDeleteTextures(GLsizei n, const GLuint* textures) {}
static inline void glPixelStorei(GLenum pname, GLint param) {}
static inline void glTexStorage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations) {}

// VAO functions
static inline void glGenVertexArrays(GLsizei n, GLuint* arrays) { for(int i=0; i<n; ++i) arrays[i] = i+1; }
static inline void glBindVertexArray(GLuint array) {}
static inline void glDeleteVertexArrays(GLsizei n, const GLuint* arrays) {}

// Framebuffer functions
static inline void glGenFramebuffers(GLsizei n, GLuint* framebuffers) { for(int i=0; i<n; ++i) framebuffers[i] = i+1; }
static inline void glBindFramebuffer(GLenum target, GLuint framebuffer) {}
static inline void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {}
static inline GLenum glCheckFramebufferStatus(GLenum target) { return GL_FRAMEBUFFER_COMPLETE; }
static inline void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {}
static inline void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {}

// Drawing functions
static inline void glDrawArrays(GLenum mode, GLint first, GLsizei count) {}
static inline void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {}
static inline void glClear(GLbitfield mask) {}
static inline void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {}
static inline void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {}
static inline void glFlush() {}
static inline void glFinish() {}

// State functions
static inline void glEnable(GLenum cap) {}
static inline void glDisable(GLenum cap) {}
static inline void glBlendFunc(GLenum sfactor, GLenum dfactor) {}
static inline void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) {}
static inline void glBlendEquation(GLenum mode) {}
static inline void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {}
static inline void glDepthMask(GLboolean flag) {}
static inline void glDepthFunc(GLenum func) {}
static inline void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {}
static inline void glCullFace(GLenum mode) {}
static inline void glFrontFace(GLenum mode) {}
static inline void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {}
static inline void glPolygonOffset(GLfloat factor, GLfloat units) {}
static inline void glStencilFunc(GLenum func, GLint ref, GLuint mask) {}
static inline void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {}

// Misc functions
static inline GLenum glGetError() { return 0; }
static inline void glGetIntegerv(GLenum pname, GLint* data) {
    if (data) {
        if (pname == GL_MAX_SAMPLES) *data = 4;
        else if (pname == 0x8D57) *data = 4; // also GL_MAX_SAMPLES
        else if (pname == 0x821D) *data = 0; // GL_NUM_EXTENSIONS
        else if (pname == 0x90EB) *data = 1024; // GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS
        else *data = 0;
    }
}
static inline const GLchar* glGetString(GLenum name) {
    if (name == GL_VERSION) return (const GLchar*)"OpenGL ES 3.1 Mock";
    if (name == GL_RENDERER) return (const GLchar*)"Mock GL Renderer";
    return (const GLchar*)"";
}
static inline const GLchar* glGetStringi(GLenum name, GLuint index) { return (const GLchar*)""; }
static inline void glMemoryBarrier(GLbitfield barriers) {}
static inline void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z) {}
static inline void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels) {}

#ifdef __cplusplus
}
#endif
