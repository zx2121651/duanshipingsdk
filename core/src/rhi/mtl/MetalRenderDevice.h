#pragma once
/**
 * MetalRenderDevice.h — Metal RHI 后端（iOS / macOS）
 *
 * 编译条件：HAS_METAL（且 __APPLE__）
 * 源文件使用 Objective-C++ (.mm)，头文件保持纯 C++ 以允许跨平台包含。
 */
#ifdef HAS_METAL

#include "../../../include/rhi/IRenderDevice.h"
#include <memory>

// Forward-declare Objective-C types as opaque C++ pointers for header portability
#ifdef __OBJC__
#   import <Metal/Metal.h>
#   import <QuartzCore/QuartzCore.h>
    using MTLDeviceRef      = id<MTLDevice>;
    using MTLCommandQueueRef= id<MTLCommandQueue>;
#else
    using MTLDeviceRef       = void*;
    using MTLCommandQueueRef = void*;
#endif

namespace sdk {
namespace video {
namespace rhi {

class MetalRenderDevice : public IRenderDevice {
public:
    ~MetalRenderDevice() override;

    /// Factory: returns nullptr if Metal unavailable
    static std::shared_ptr<MetalRenderDevice> tryCreate();

    // IRenderDevice
    std::shared_ptr<ITexture>       createTexture(const TextureDesc& desc) override;
    std::shared_ptr<IBuffer>        createBuffer(BufferType, BufferUsage, size_t, const void*) override;
    std::shared_ptr<IVertexArray>   createVertexArray() override;
    std::shared_ptr<IPipelineState> createGraphicsPipeline(const PipelineStateDesc& desc) override;
    std::shared_ptr<IShaderResourceSet> createShaderResourceSet() override;
    std::shared_ptr<ICommandBuffer> createCommandBuffer() override;
    std::shared_ptr<IShaderProgram> createShaderProgram(const char* vert, const char* frag) override;
    std::shared_ptr<IShaderProgram> createGeometryShaderProgram(const char*, const char*, const char*) override;
    std::shared_ptr<IShaderProgram> createTessellationProgram(const char*, const char*, const char*, const char*) override;
    std::shared_ptr<ITexture>       createMSAATexture(const TextureDesc& desc, int samples) override;
    void submit(ICommandBuffer* cmdBuffer) override;
    void waitIdle() override;
    std::shared_ptr<ITexture> createTextureFromHardwareBuffer(const HardwareBufferDesc& desc) override;
    RHICapabilities getCapabilities() const override;

    MTLDeviceRef      mtlDevice()       const { return m_device; }
    MTLCommandQueueRef mtlCommandQueue() const { return m_commandQueue; }

private:
    MetalRenderDevice() = default;
    bool init();

    MTLDeviceRef       m_device       = nullptr;
    MTLCommandQueueRef m_commandQueue = nullptr;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_METAL
