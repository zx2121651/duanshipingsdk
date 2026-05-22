#include "../../include/rhi/GLHardwareTexture.h"

#ifdef USE_MOCK_GL
    #include "GLES3/gl3.h"
#elif defined(__APPLE__)
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES2/glext.h>
    #include <OpenGLES/ES3/glext.h>
#else
    #include <GLES3/gl3.h>
    #include <GLES3/gl31.h>
    #include <GLES2/gl2ext.h>
#endif

#define LOG_TAG "GLHardwareTexture"
#include "../../include/Log.h"

namespace sdk {
namespace video {
namespace rhi {

// Always define the fallback constructor to resolve linker errors in memory:2278
GLHardwareTexture::GLHardwareTexture(uint32_t id, uint32_t target, uint32_t width, uint32_t height)
    : m_id(id), m_target(target), m_width(width), m_height(height) {
#if defined(__ANDROID__)
    m_eglImage = EGL_NO_IMAGE_KHR;
#elif defined(__APPLE__)
    m_cvTexture = nullptr;
#endif
}

#if defined(__ANDROID__)

GLHardwareTexture::GLHardwareTexture(uint32_t id, uint32_t target, uint32_t width, uint32_t height, EGLImageKHR eglImage)
    : m_id(id), m_target(target), m_width(width), m_height(height), m_eglImage(eglImage) {
}

GLHardwareTexture::~GLHardwareTexture() {
    if (m_id != 0) {
        glDeleteTextures(1, &m_id);
        m_id = 0;
    }

    if (m_eglImage != EGL_NO_IMAGE_KHR) {
        EGLDisplay display = eglGetCurrentDisplay();
        if (display != EGL_NO_DISPLAY) {
            static auto s_eglDestroyImageKHR =
                (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
            if (s_eglDestroyImageKHR) {
                s_eglDestroyImageKHR(display, m_eglImage);
            }
        }
        m_eglImage = EGL_NO_IMAGE_KHR;
    }
}

#elif defined(__APPLE__)

GLHardwareTexture::GLHardwareTexture(uint32_t id, uint32_t target, uint32_t width, uint32_t height, CVOpenGLESTextureRef cvTexture)
    : m_id(id), m_target(target), m_width(width), m_height(height), m_cvTexture(cvTexture) {
}

GLHardwareTexture::~GLHardwareTexture() {
    if (m_id != 0) {
        if (!m_cvTexture) {
            glDeleteTextures(1, &m_id);
        }
        m_id = 0;
    }

    if (m_cvTexture != nullptr) {
        CFRelease(m_cvTexture);
        m_cvTexture = nullptr;
    }
}

#else

GLHardwareTexture::~GLHardwareTexture() {
    if (m_id != 0) {
        glDeleteTextures(1, &m_id);
        m_id = 0;
    }
}

#endif

} // namespace rhi
} // namespace video
} // namespace sdk
