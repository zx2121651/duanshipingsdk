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
};

} // namespace rhi
} // namespace video
} // namespace sdk
