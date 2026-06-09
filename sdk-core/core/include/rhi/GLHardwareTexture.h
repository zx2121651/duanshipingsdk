#pragma once
#include "ITexture.h"
#include "ExternalTexture.h"
#include "../GLTypes.h"
#include <array>
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
    GLHardwareTexture(uint32_t id,
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
                      const std::array<float, 16>& transformMatrix);
#elif defined(__APPLE__)
    GLHardwareTexture(uint32_t id,
                      uint32_t target,
                      uint32_t width,
                      uint32_t height,
                      TextureFormat format,
                      CVOpenGLESTextureRef cvTexture,
                      uint64_t timestampNs,
                      const std::array<float, 16>& transformMatrix);
#endif

    // Fallback/Stub constructor always available
    GLHardwareTexture(uint32_t id,
                      uint32_t target,
                      uint32_t width,
                      uint32_t height,
                      TextureFormat format = TextureFormat::RGBA8,
                      uint64_t timestampNs = 0,
                      const std::array<float, 16>& transformMatrix = identityTextureTransform());


    ~GLHardwareTexture() override;

    uint32_t getWidth() const override { return m_width; }
    uint32_t getHeight() const override { return m_height; }
    uint32_t getId() const override { return m_id; }
    uint32_t getTarget() const override { return m_target; }
    TextureFormat getFormat() const override { return m_format; }
    uint64_t getTimestampNs() const { return m_timestampNs; }
    const std::array<float, 16>& getTransformMatrix() const { return m_transformMatrix; }
    uint32_t getStride() const { return m_stride; }
    uint32_t getLayers() const { return m_layers; }
    int getNativeFormat() const { return m_nativeFormat; }
    uint64_t getUsage() const { return m_usage; }

private:
    uint32_t m_id;
    uint32_t m_target;
    uint32_t m_width;
    uint32_t m_height;
    TextureFormat m_format;
    uint64_t m_timestampNs;
    std::array<float, 16> m_transformMatrix;
    uint32_t m_stride = 0;
    uint32_t m_layers = 1;
    int m_nativeFormat = 0;
    uint64_t m_usage = 0;

#if defined(__ANDROID__)
    EGLImageKHR m_eglImage = EGL_NO_IMAGE_KHR;
    void* m_nativeBuffer = nullptr;
    ExternalOwnership m_ownership = ExternalOwnership::Borrowed;
#elif defined(__APPLE__)
    CVOpenGLESTextureRef m_cvTexture = nullptr;
#endif
};

} // namespace rhi
} // namespace video
} // namespace sdk
