#pragma once

#include "ITexture.h"
#include <array>
#include <cstdint>

namespace sdk {
namespace video {
namespace rhi {

enum class ExternalTextureSource {
    Unknown,
    GLTexture2D,
    GLOESTexture,
    AndroidHardwareBuffer,
    IOSCVPixelBuffer,
    VulkanImage,
    MetalTexture
};

enum class ExternalOwnership {
    Borrowed,
    Retain,
    TakeOwnership
};

enum class ExternalSyncMode {
    None,
    CpuWait,
    GpuWait
};

inline std::array<float, 16> identityTextureTransform() {
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
}

struct HardwareBufferDesc {
    void* nativeBuffer = nullptr; // Android: AHardwareBuffer* ; iOS: CVPixelBufferRef
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t layers = 1;
    int format = 0; // OS-specific native format hint
    uint64_t usage = 0;
    TextureFormat logicalFormat = TextureFormat::RGBA8;
    ExternalTextureSource source = ExternalTextureSource::Unknown;
    ExternalOwnership ownership = ExternalOwnership::Borrowed;
    ExternalSyncMode syncMode = ExternalSyncMode::GpuWait;
    int acquireFenceFd = -1; // Android native fence fd; consumed/closed by the importer when >= 0.
    uint64_t timestampNs = 0;
    std::array<float, 16> transformMatrix = identityTextureTransform();
};

struct ExternalTextureDesc {
    uint64_t handle = 0; // Backend-specific existing texture/image handle.
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    uint32_t target = 0; // GLES: GL_TEXTURE_2D / GL_TEXTURE_EXTERNAL_OES.
    ExternalTextureSource source = ExternalTextureSource::Unknown;
    bool ownsHandle = false;
    uint64_t timestampNs = 0;
    std::array<float, 16> transformMatrix = identityTextureTransform();
};

} // namespace rhi
} // namespace video
} // namespace sdk
