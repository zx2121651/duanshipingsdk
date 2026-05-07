#pragma once
#ifdef __APPLE__
#include "../IRenderDevice.h"
#include <memory>

// Forward-declare Metal ObjC types without importing Metal.h in a plain header.
// The .mm implementation will include <Metal/Metal.h> directly.
#ifdef __OBJC__
@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLLibrary;
#else
typedef struct objc_object MTLDevice;
typedef struct objc_object MTLCommandQueue;
typedef struct objc_object MTLLibrary;
#endif

namespace sdk {
namespace video {
namespace rhi {

/**
 * MetalRenderDevice
 *
 * Apple Metal implementation of [IRenderDevice].
 * Lifecycle:
 *   1. construct with the default MTLDevice (MTLCreateSystemDefaultDevice)
 *   2. call initialize() — compiles the built-in shader library
 *   3. use createTexture / createBuffer / createShaderProgram for resources
 *   4. call shutdown() before destruction
 *
 * Thread safety: all calls must be made from a single dedicated render thread
 * (or protected externally). Metal command queues are thread-safe, but the
 * resource factory methods are not.
 */
class MetalRenderDevice final : public IRenderDevice {
public:
    MetalRenderDevice();
    ~MetalRenderDevice() override;

    // Metal-specific lifecycle (NOT overrides of IRenderDevice — Metal has its own init)
    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // -----------------------------------------------------------------------
    // IRenderDevice — core factory methods (signatures aligned with GLES/Vulkan)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::shared_ptr<ITexture> createTexture(
        const TextureDesc& desc) override;
    [[nodiscard]] std::shared_ptr<IBuffer> createBuffer(
        BufferType type, BufferUsage usage, size_t size,
        const void* data = nullptr) override;
    [[nodiscard]] std::shared_ptr<IVertexArray>   createVertexArray() override;
    [[nodiscard]] std::shared_ptr<IPipelineState>  createGraphicsPipeline(
        const PipelineStateDesc& desc) override;
    [[nodiscard]] std::shared_ptr<IShaderResourceSet> createShaderResourceSet() override;
    [[nodiscard]] std::shared_ptr<ICommandBuffer>  createCommandBuffer() override;
    [[nodiscard]] std::shared_ptr<IShaderProgram>  createShaderProgram(
        const char* vertexSrc, const char* fragmentSrc) override;
    [[nodiscard]] std::shared_ptr<IShaderProgram>  createGeometryShaderProgram(
        const char*, const char*, const char*) override;
    [[nodiscard]] std::shared_ptr<IShaderProgram>  createTessellationProgram(
        const char*, const char*, const char*, const char*) override;
    [[nodiscard]] std::shared_ptr<ITexture> createMSAATexture(
        const TextureDesc& desc, int samples) override;
    void submit(ICommandBuffer* cmdBuffer) override;
    [[nodiscard]] std::shared_ptr<ITexture> bindExternalHardwareBuffer(
        void* nativeBuffer) override;
    [[nodiscard]] RHICapabilities getCapabilities() const override;

    // Metal-specific extra: create texture from a CVPixelBuffer / MTLTexture handle
    [[nodiscard]] std::shared_ptr<ITexture> createTextureFromHandle(
        uint64_t nativeHandle, int width, int height, TextureFormat format);

    // Metal-specific accessors (used by the rest of the iOS SDK)
    void* getMTLDevice()       const { return m_device; }
    void* getMTLCommandQueue() const { return m_commandQueue; }

private:
    bool  m_initialized  = false;

    // Raw ObjC pointers stored as void* so the header stays pure C++.
    // The .mm file casts them back to id<MTLDevice> etc.
    void* m_device       = nullptr;  // id<MTLDevice>
    void* m_commandQueue = nullptr;  // id<MTLCommandQueue>
    void* m_library      = nullptr;  // id<MTLLibrary> (default shader library)
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // __APPLE__
