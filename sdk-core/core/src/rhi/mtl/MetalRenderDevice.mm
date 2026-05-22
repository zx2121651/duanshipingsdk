/**
 * MetalRenderDevice.mm — Metal RHI 后端实现
 * 编译环境：Xcode / clang with -fobjc-arc, HAS_METAL defined
 */
#ifdef HAS_METAL
#import <Metal/Metal.h>

#include "MetalRenderDevice.h"
#include "MetalTexture.h"
#include "MetalBuffer.h"
#include "MetalShaderProgram.h"
#include "MetalCommandBuffer.h"
#include "MetalPipeline.h"
#include "../GLVertexArray.h"   // reuse for vertex layout description
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

// ---------------------------------------------------------------------------
std::shared_ptr<MetalRenderDevice> MetalRenderDevice::tryCreate() {
    auto dev = std::shared_ptr<MetalRenderDevice>(new MetalRenderDevice());
    if (!dev->init()) return nullptr;
    return dev;
}

bool MetalRenderDevice::init() {
    m_device = MTLCreateSystemDefaultDevice();
    if (!m_device) {
        std::cerr << "MetalRenderDevice: MTLCreateSystemDefaultDevice() returned nil" << std::endl;
        return false;
    }
    m_commandQueue = [m_device newCommandQueue];
    if (!m_commandQueue) {
        std::cerr << "MetalRenderDevice: newCommandQueue() returned nil" << std::endl;
        return false;
    }
    NSLog(@"MetalRenderDevice: initialized on GPU: %@", [m_device name]);
    return true;
}

MetalRenderDevice::~MetalRenderDevice() {
    // ARC handles deallocation of Objective-C objects
}

// ---------------------------------------------------------------------------
std::shared_ptr<ITexture> MetalRenderDevice::createTexture(const TextureDesc& desc) {
    return std::make_shared<MetalTexture>(m_device, desc, 1);
}

std::shared_ptr<ITexture> MetalRenderDevice::createMSAATexture(
    const TextureDesc& desc, int samples)
{
    return std::make_shared<MetalTexture>(m_device, desc, samples);
}

std::shared_ptr<IBuffer> MetalRenderDevice::createBuffer(
    BufferType type, BufferUsage usage, size_t size, const void* data)
{
    return std::make_shared<MetalBuffer>(m_device, type, usage, size, data);
}

std::shared_ptr<IVertexArray> MetalRenderDevice::createVertexArray() {
    return std::make_shared<GLVertexArray>(); // layout tracking only
}

std::shared_ptr<IPipelineState> MetalRenderDevice::createGraphicsPipeline(
    const PipelineStateDesc& desc)
{
    auto pso = std::make_shared<MetalPipelineState>();
    pso->desc = desc;
    return pso;
}

std::shared_ptr<IShaderResourceSet> MetalRenderDevice::createShaderResourceSet() {
    return std::make_shared<MetalResourceSet>();
}

std::shared_ptr<ICommandBuffer> MetalRenderDevice::createCommandBuffer() {
    return std::make_shared<MetalCommandBuffer>(this);
}

std::shared_ptr<IShaderProgram> MetalRenderDevice::createShaderProgram(
    const char* vertMSL, const char* fragMSL)
{
    return MetalShaderProgram::createFromMSL(m_device, vertMSL, fragMSL);
}

std::shared_ptr<IShaderProgram> MetalRenderDevice::createGeometryShaderProgram(
    const char* /*vert*/, const char* /*geom*/, const char* /*frag*/)
{
    // Metal does not have a separate geometry shader stage.
    // Geometry operations are done via mesh shaders (iOS 16+) or compute.
    std::cerr << "MetalRenderDevice: Geometry shaders not supported in Metal — use mesh shaders or compute" << std::endl;
    return nullptr;
}

std::shared_ptr<IShaderProgram> MetalRenderDevice::createTessellationProgram(
    const char* /*vert*/, const char* tescMSL, const char* /*tese*/, const char* fragMSL)
{
    // Metal tessellation: kernel (post-tessellation vertex function) + fragment
    return MetalShaderProgram::createTessellationFromMSL(m_device, tescMSL, fragMSL);
}

void MetalRenderDevice::submit(ICommandBuffer* cmdBuffer) {
    auto* mtlCmd = static_cast<MetalCommandBuffer*>(cmdBuffer);
    if (mtlCmd) mtlCmd->commit();
}

void MetalRenderDevice::waitIdle() {
    // There is no global waitIdle in Metal; submit an empty command buffer and wait for its completion.
    MTLCommandBufferRef cmdbuf = [m_commandQueue commandBuffer];
    [cmdbuf commit];
    [cmdbuf waitUntilCompleted];
}

std::shared_ptr<ITexture> MetalRenderDevice::createTextureFromHardwareBuffer(const HardwareBufferDesc& desc) {
    // On iOS, nativeBuffer is a CVPixelBufferRef or IOSurface
    // Use CVMetalTextureCache for zero-copy binding (simplified stub here)
    std::cerr << "MetalRenderDevice::bindExternalHardwareBuffer — stub (use CVMetalTextureCache)" << std::endl;
    (void)nativeBuffer;
    return nullptr;
}

RHICapabilities MetalRenderDevice::getCapabilities() const {
    RHICapabilities caps;
    caps.backend = BackendType::METAL;

    // All Apple GPUs since A7 support compute
    caps.computeShader = true;

    // Metal does not have a traditional geometry shader stage
    caps.geometryShader = false;

    // Metal supports tessellation (iOS 10+ / macOS 10.12+)
    caps.tessellation = ([m_device supportsFeatureSet:
#if TARGET_OS_IPHONE
        MTLFeatureSet_iOS_GPUFamily3_v1
#else
        MTLFeatureSet_macOS_GPUFamily1_v2
#endif
    ]);

    // MSAA
    caps.msaa = ([m_device supportsTextureSampleCount:4]);
    caps.maxMSAASamples = caps.msaa ? 8 : 1;

    // FP16 render targets (A9+)
    caps.fp16RenderTarget = true;

    // ASTC compression
    caps.astc = ([m_device supportsFeatureSet:
#if TARGET_OS_IPHONE
        MTLFeatureSet_iOS_GPUFamily2_v1
#else
        MTLFeatureSet_macOS_GPUFamily1_v1
#endif
    ]);

    caps.glesVersionInt = 0;
    caps.rendererString = std::string([[m_device name] UTF8String]);
    return caps;
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_METAL
