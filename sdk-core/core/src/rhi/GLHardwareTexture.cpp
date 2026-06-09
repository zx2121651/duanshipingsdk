#include "../../include/rhi/GLHardwareTexture.h"

#ifdef USE_MOCK_GL
    #include "GLES3/gl3.h"
#elif defined(__APPLE__)
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES2/glext.h>
    #include <OpenGLES/ES3/glext.h>
#elif defined(__ANDROID__)
#if __ANDROID_API__ >= 26
    #include <android/hardware_buffer.h>
#endif
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
    #include <GLES3/gl3.h>
    #include <GLES3/gl31.h>
    #include <GLES2/gl2ext.h>
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
GLHardwareTexture::GLHardwareTexture(uint32_t id,
                                     uint32_t target,
                                     uint32_t width,
                                     uint32_t height,
                                     TextureFormat format,
                                     uint64_t timestampNs,
                                     const std::array<float, 16>& transformMatrix)
    : m_id(id),
      m_target(target),
      m_width(width),
      m_height(height),
      m_format(format),
      m_timestampNs(timestampNs),
      m_transformMatrix(transformMatrix) {
#if defined(__ANDROID__)
    m_eglImage = EGL_NO_IMAGE_KHR;
    m_nativeBuffer = nullptr;
    m_ownership = ExternalOwnership::Borrowed;
#elif defined(__APPLE__)
    m_cvTexture = nullptr;
#endif
}

#if defined(__ANDROID__)

GLHardwareTexture::GLHardwareTexture(uint32_t id,
                                     uint32_t target,
                                     uint32_t width,
                                     uint32_t height,
                                     TextureFormat format,
                                     EGLImageKHR eglImage,
                                     void* nativeBuffer,
                                     ExternalOwnership ownership,
                                     uint32_t stride,
                                     uint32_t layers,
                                     int nativeFormat,
                                     uint64_t usage,
                                     uint64_t timestampNs,
                                     const std::array<float, 16>& transformMatrix)
    : m_id(id),
      m_target(target),
      m_width(width),
      m_height(height),
      m_format(format),
      m_timestampNs(timestampNs),
      m_transformMatrix(transformMatrix),
      m_stride(stride),
      m_layers(layers),
      m_nativeFormat(nativeFormat),
      m_usage(usage),
      m_eglImage(eglImage),
      m_nativeBuffer(nativeBuffer),
      m_ownership(ownership) {
#if __ANDROID_API__ >= 26
    if (m_nativeBuffer && m_ownership == ExternalOwnership::Retain) {
        AHardwareBuffer_acquire(static_cast<AHardwareBuffer*>(m_nativeBuffer));
    }
#endif
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

#if __ANDROID_API__ >= 26
    if (m_nativeBuffer &&
        (m_ownership == ExternalOwnership::Retain ||
         m_ownership == ExternalOwnership::TakeOwnership)) {
        AHardwareBuffer_release(static_cast<AHardwareBuffer*>(m_nativeBuffer));
    }
#endif
    m_nativeBuffer = nullptr;
}

#elif defined(__APPLE__)

GLHardwareTexture::GLHardwareTexture(uint32_t id,
                                     uint32_t target,
                                     uint32_t width,
                                     uint32_t height,
                                     TextureFormat format,
                                     CVOpenGLESTextureRef cvTexture,
                                     uint64_t timestampNs,
                                     const std::array<float, 16>& transformMatrix)
    : m_id(id),
      m_target(target),
      m_width(width),
      m_height(height),
      m_format(format),
      m_timestampNs(timestampNs),
      m_transformMatrix(transformMatrix),
      m_cvTexture(cvTexture) {
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
