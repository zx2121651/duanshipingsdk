#pragma once

#include "GLTypes.h"

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
