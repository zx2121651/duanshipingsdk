#pragma once
#include "ITexture.h"
#include "../GLTypes.h"
#include <cstdint>

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#elif defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#endif

namespace sdk {
namespace video {
namespace rhi {

/**
 * GLHardwareTexture
 *
 * An RHI texture implementation that wraps external hardware buffers
 * (AHardwareBuffer on Android, CVPixelBuffer on iOS).
 * Ensures correct RAII lifecycle management of EGLImageKHR or CVOpenGLESTextureRef.
 */
class GLHardwareTexture : public ITexture {
public:
#if defined(__ANDROID__)
    GLHardwareTexture(uint32_t id, uint32_t target, uint32_t width, uint32_t height, EGLImageKHR eglImage);
#elif defined(__APPLE__)
    GLHardwareTexture(uint32_t id, uint32_t target, uint32_t width, uint32_t height, CVOpenGLESTextureRef cvTexture);
#endif

    // Fallback/Stub constructor always available
    GLHardwareTexture(uint32_t id, uint32_t target, uint32_t width, uint32_t height);


    ~GLHardwareTexture() override;

    uint32_t getWidth() const override { return m_width; }
    uint32_t getHeight() const override { return m_height; }
    uint32_t getId() const override { return m_id; }
    TextureFormat getFormat() const override { return TextureFormat::RGBA8; } // Logical default
    uint32_t getTarget() const { return m_target; }

private:
    uint32_t m_id;
    uint32_t m_target;
    uint32_t m_width;
    uint32_t m_height;

#if defined(__ANDROID__)
    EGLImageKHR m_eglImage = EGL_NO_IMAGE_KHR;
#elif defined(__APPLE__)
    CVOpenGLESTextureRef m_cvTexture = nullptr;
#endif
};

} // namespace rhi
} // namespace video
} // namespace sdk
