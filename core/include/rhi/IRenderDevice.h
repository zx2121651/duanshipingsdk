#pragma once
#include <memory>
#include <cstdint>
#include "ITexture.h"
#include "IBuffer.h"
#include "IVertexArray.h"
#include "ICommandBuffer.h"
#include "IPipelineState.h"
#include "IShaderProgram.h"

namespace sdk {
namespace video {
namespace rhi {

// ---------------------------------------------------------------------------
// BackendType — canonical definition (was previously in RenderDeviceFactory.h)
// ---------------------------------------------------------------------------
enum class BackendType {
    AUTO   = 0,  ///< 自动选择（优先级：Metal > Vulkan > GLES）
    GLES   = 1,  ///< OpenGL ES（3.0 / 3.1 / 3.2 三级梯级）
    VULKAN = 2,  ///< Vulkan（Android NDK）
    METAL  = 3   ///< Metal（iOS / macOS）
};

inline const char* backendTypeName(BackendType t) {
    switch (t) {
        case BackendType::AUTO:   return "AUTO";
        case BackendType::GLES:   return "GLES";
        case BackendType::VULKAN: return "VULKAN";
        case BackendType::METAL:  return "METAL";
        default:                  return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// RHICapabilities — backend capability snapshot, returned by getCapabilities()
// Callers can query what the active backend supports without touching
// GLContextManager fields directly.
// ---------------------------------------------------------------------------
struct RHICapabilities {
    BackendType backend          = BackendType::GLES;
    bool        computeShader    = false; ///< GLES 3.1+ compute with ≥256 invocations / Vulkan
    bool        geometryShader   = false; ///< GLES 3.2 core or OES extension / Vulkan
    bool        tessellation     = false; ///< GLES 3.2 core or OES extension / Vulkan
    bool        msaa             = false; ///< maxMSAASamples >= 4
    bool        fp16RenderTarget = false; ///< GL_RGBA16F as FBO attachment
    bool        astc             = false; ///< GL_KHR_texture_compression_astc_ldr
    int         maxMSAASamples   = 1;
    int         glesVersionInt   = 30;    ///< 30/31/32; 0 for Vulkan/Metal
    const char* rendererString   = "";    ///< GL_RENDERER or Metal device name
};

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    [[nodiscard]] virtual std::shared_ptr<ITexture> createTexture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual std::shared_ptr<IBuffer> createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data = nullptr) = 0;
    [[nodiscard]] virtual std::shared_ptr<IVertexArray> createVertexArray() = 0;

    [[nodiscard]] virtual std::shared_ptr<IPipelineState> createGraphicsPipeline(const PipelineStateDesc& desc) = 0;
    [[nodiscard]] virtual std::shared_ptr<IShaderResourceSet> createShaderResourceSet() = 0;

    [[nodiscard]] virtual std::shared_ptr<ICommandBuffer> createCommandBuffer() = 0;

    /**
     * @brief Convenience factory: compile and link a graphics shader program.
     * Replaces the pattern of manually calling GLShaderProgram ctor outside the RHI.
     */
    [[nodiscard]] virtual std::shared_ptr<IShaderProgram> createShaderProgram(
        const char* vertexSrc, const char* fragmentSrc) = 0;

    /**
     * GLES 3.2 / Vulkan / Metal — 几何着色器程序（不支持时返回 nullptr）
     * @param vertSrc   顶点着色器源码
     * @param geomSrc   几何着色器源码（#version 320 es）
     * @param fragSrc   片元着色器源码
     */
    [[nodiscard]] virtual std::shared_ptr<IShaderProgram> createGeometryShaderProgram(
        const char* vertSrc, const char* geomSrc, const char* fragSrc) = 0;

    /**
     * GLES 3.2 / Vulkan / Metal — 细分着色器程序（不支持时返回 nullptr）
     */
    [[nodiscard]] virtual std::shared_ptr<IShaderProgram> createTessellationProgram(
        const char* vertSrc, const char* tescSrc,
        const char* teseSrc, const char* fragSrc) = 0;

    /**
     * GLES 3.2 / Vulkan / Metal — 创建多采样纹理（MSAA）
     * @param samples  采样数（2/4/8/16，超过 maxSamples 自动降级）
     */
    [[nodiscard]] virtual std::shared_ptr<ITexture> createMSAATexture(
        const TextureDesc& desc, int samples) = 0;

    virtual void submit(ICommandBuffer* cmdBuffer) = 0;

    // External bindings
    virtual std::shared_ptr<ITexture> bindExternalHardwareBuffer(void* nativeBuffer) = 0;

    /**
     * @brief Query backend capability snapshot.
     * Safe to call without an active GL context (returns conservative defaults).
     */
    [[nodiscard]] virtual RHICapabilities getCapabilities() const = 0;

    /**
     * @brief Global resource synchronization.
     * Flushes commands to the GPU and blocks until all previously submitted commands are complete.
     */
    virtual void waitIdle() = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
