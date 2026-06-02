#pragma once

#include "GLTypes.h"

// ------------------------------------------------------------------
// GL 错误追踪宏 — 在每个关键 GL 操作后插入，精确定位 0x502 来源
// ------------------------------------------------------------------
#ifdef __ANDROID__
#include <android/log.h>
#define CHECK_GL_ERROR_LINE()                                  \
{                                                               \
    GLenum e = glGetError();                                    \
    if (e != GL_NO_ERROR) {                                     \
        __android_log_print(ANDROID_LOG_ERROR, "GL_CRITICAL",  \
            "File: %s, Line: %d, GL error: 0x%X",             \
            __FILE__, __LINE__, e);                             \
    }                                                           \
}
#else
#define CHECK_GL_ERROR_LINE()                                  \
{                                                               \
    GLenum e = glGetError();                                    \
    if (e != GL_NO_ERROR) {                                     \
        std::cerr << "[GL_CRITICAL] File: " << __FILE__         \
                  << ", Line: " << __LINE__                     \
                  << ", GL error: 0x" << std::hex << e          \
                  << std::dec << std::endl;                     \
    }                                                           \
}
#endif

namespace sdk {
namespace video {

/**
 * @brief 轻量级 OpenGL 状态机缓存 (Lightweight GL State Caching)
 *
 * 用于在 C++ 层拦截和过滤冗余的 GL 状态切换指令，降低 GPU 驱动层的 CPU 开销。
 * 支持基于 thread_local 的单例模式，因为 OpenGL Context 本身就是线程绑定的。
 */
class GLStateManager {
public:
    static GLStateManager& getInstance();

    // 常用状态绑定
    void useProgram(GLuint program);
    void bindFramebuffer(GLenum target, GLuint framebuffer);
    void bindTexture(GLenum target, GLuint texture);
    void activeTexture(GLenum textureUnit);

    // 顶点属性数组使能
    void enableVertexAttribArray(GLuint index);
    void disableVertexAttribArray(GLuint index);

    // 其他开关状态 (glEnable / glDisable)
    void enable(GLenum cap);
    void disable(GLenum cap);

    // 强制清空缓存（例如切后台再回来 Context 重建后）
    void invalidateCache();

    GLStateManager() { invalidateCache(); }
    ~GLStateManager() = default;

private:
    GLStateManager(const GLStateManager&) = delete;
    GLStateManager& operator=(const GLStateManager&) = delete;

    GLuint m_currentProgram = 0;
    GLuint m_currentFramebuffer = 0;
    GLenum m_activeTextureUnit = GL_TEXTURE0;

    GLuint m_boundTexture2D[32] = {0};
#ifdef __ANDROID__
    GLuint m_boundTextureOES[32] = {0};
#endif

    bool m_vertexAttribArrayEnabled[16] = {false};
};

} // namespace video
} // namespace sdk
