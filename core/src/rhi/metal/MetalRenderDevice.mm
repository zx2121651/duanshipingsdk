// MetalRenderDevice.mm — Apple Metal IRenderDevice implementation
// Compiled only on Apple platforms (iOS / macOS) via Xcode or CMake with
//   target_sources(... PRIVATE MetalRenderDevice.mm)
//   set_source_files_properties(... PROPERTIES COMPILE_FLAGS "-x objective-c++")
#ifdef __APPLE__

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "../../../include/rhi/metal/MetalRenderDevice.h"
#include "../../../include/rhi/metal/MetalTexture.h"
#include "../../../include/rhi/metal/MetalBuffer.h"
#include "../../../include/rhi/metal/MetalShaderProgram.h"

#define LOG_TAG "MetalRenderDevice"
#include "../../../include/Log.h"

namespace sdk {
namespace video {
namespace rhi {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static inline id<MTLDevice> device(const MetalRenderDevice* self) {
    return (__bridge id<MTLDevice>)self->getMTLDevice();
}
static inline id<MTLCommandQueue> queue(const MetalRenderDevice* self) {
    return (__bridge id<MTLCommandQueue>)self->getMTLCommandQueue();
}

// ---------------------------------------------------------------------------
MetalRenderDevice::MetalRenderDevice() = default;

MetalRenderDevice::~MetalRenderDevice() {
    shutdown();
}

bool MetalRenderDevice::initialize() {
    if (m_initialized) return true;

    // 1. Pick the system default GPU
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) {
        LOGE("MTLCreateSystemDefaultDevice() returned nil — Metal not supported");
        return false;
    }
    m_device = (__bridge_retained void*)dev;
    LOGI("Metal device: %s", [[dev name] UTF8String]);

    // 2. Create a serial command queue for render work
    id<MTLCommandQueue> cq = [dev newCommandQueue];
    if (!cq) {
        LOGE("Failed to create MTLCommandQueue");
        CFRelease(m_device);
        m_device = nullptr;
        return false;
    }
    m_commandQueue = (__bridge_retained void*)cq;

    // 3. Compile the default shader library (Metal shaders are pre-compiled
    //    into the app bundle as default.metallib at build time)
    NSError* err = nil;
    id<MTLLibrary> lib = [dev newDefaultLibrary];
    if (!lib) {
        // Fallback: try loading from bundle explicitly
        NSBundle* bundle = [NSBundle bundleForClass:[NSObject class]];
        NSString* libPath = [bundle pathForResource:@"default" ofType:@"metallib"];
        if (libPath) {
            lib = [dev newLibraryWithFile:libPath error:&err];
        }
    }
    if (!lib) {
        LOGW("Default Metal shader library not found — shader programs will compile inline");
    } else {
        m_library = (__bridge_retained void*)lib;
    }

    m_initialized = true;
    LOGI("MetalRenderDevice initialized");
    return true;
}

void MetalRenderDevice::shutdown() {
    if (!m_initialized) return;

    if (m_library)       { CFRelease(m_library);       m_library       = nullptr; }
    if (m_commandQueue)  { CFRelease(m_commandQueue);  m_commandQueue  = nullptr; }
    if (m_device)        { CFRelease(m_device);        m_device        = nullptr; }

    m_initialized = false;
    LOGI("MetalRenderDevice shutdown");
}

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------
std::shared_ptr<ITexture> MetalRenderDevice::createTexture(const TextureDesc& desc)
{
    id<MTLDevice> dev = (__bridge id<MTLDevice>)m_device;
    if (!dev) return nullptr;

    // Map TextureFormat → MTLPixelFormat
    MTLPixelFormat pixFmt = MTLPixelFormatBGRA8Unorm;
    if (desc.format == TextureFormat::RGBA16F) pixFmt = MTLPixelFormatRGBA16Float;
    else if (desc.format == TextureFormat::Depth24) pixFmt = MTLPixelFormatDepth32Float;

    MTLTextureDescriptor* mtlDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:pixFmt
                                     width:(NSUInteger)desc.width
                                    height:(NSUInteger)desc.height
                                 mipmapped:NO];
    mtlDesc.usage       = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    mtlDesc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> tex = [dev newTextureWithDescriptor:mtlDesc];
    if (!tex) {
        LOGE("createTexture failed (%ux%u)", desc.width, desc.height);
        return nullptr;
    }
    return std::make_shared<MetalTexture>(
        (__bridge_retained void*)tex,
        static_cast<int>(desc.width), static_cast<int>(desc.height), desc.format);
}

std::shared_ptr<ITexture> MetalRenderDevice::createTextureFromHandle(
    uint64_t nativeHandle, int width, int height, TextureFormat format)
{
    // nativeHandle is a CVPixelBufferRef or MTLTexture pointer bridged to uint64_t
    id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)(uintptr_t)nativeHandle;
    if (!tex) return nullptr;

    return std::make_shared<MetalTexture>(
        (__bridge_retained void*)tex, width, height, format);
}

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------
std::shared_ptr<IBuffer> MetalRenderDevice::createBuffer(
    BufferType /*type*/, BufferUsage usage, size_t sizeBytes, const void* data)
{
    id<MTLDevice> dev = (__bridge id<MTLDevice>)m_device;
    if (!dev) return nullptr;
    MTLResourceOptions opts = (usage == BufferUsage::StaticDraw)
        ? MTLResourceStorageModePrivate
        : MTLResourceStorageModeShared;

    id<MTLBuffer> buf = data
        ? [dev newBufferWithBytes:data length:sizeBytes options:opts]
        : [dev newBufferWithLength:sizeBytes options:opts];
    if (!buf) {
        LOGE("createBuffer(%zu) failed", sizeBytes);
        return nullptr;
    }
    return std::make_shared<MetalBuffer>((__bridge_retained void*)buf, sizeBytes);
}

// ---------------------------------------------------------------------------
// Stubs — IRenderDevice pure virtuals without Metal equivalents
// ---------------------------------------------------------------------------
std::shared_ptr<IVertexArray> MetalRenderDevice::createVertexArray() {
    return nullptr; // Vertex layout is embedded in PSO on Metal
}
std::shared_ptr<IPipelineState> MetalRenderDevice::createGraphicsPipeline(
    const PipelineStateDesc& /*desc*/)
{
    return nullptr; // Metal PSO creation requires MTLRenderPipelineDescriptor (future work)
}
std::shared_ptr<IShaderResourceSet> MetalRenderDevice::createShaderResourceSet() {
    return nullptr;
}
std::shared_ptr<ICommandBuffer> MetalRenderDevice::createCommandBuffer() {
    return nullptr; // Metal uses MTLCommandBuffer; ICommandBuffer bridge is future work
}
std::shared_ptr<IShaderProgram> MetalRenderDevice::createGeometryShaderProgram(
    const char*, const char*, const char*)
{
    LOGW("MetalRenderDevice: geometry shaders are not supported on Metal");
    return nullptr;
}
std::shared_ptr<IShaderProgram> MetalRenderDevice::createTessellationProgram(
    const char*, const char*, const char*, const char*)
{
    LOGW("MetalRenderDevice: tessellation via this path is not supported; use Metal compute");
    return nullptr;
}
std::shared_ptr<ITexture> MetalRenderDevice::createMSAATexture(
    const TextureDesc& desc, int /*samples*/)
{
    return createTexture(desc); // Metal MSAA is configured per-attachment; treat as regular texture
}
void MetalRenderDevice::submit(ICommandBuffer* /*cmdBuffer*/) {
    // ICommandBuffer bridge not yet implemented; Metal commands are submitted via MTLCommandQueue
}

void MetalRenderDevice::waitIdle() {
    if (m_commandQueue) {
        id<MTLCommandQueue> cq = (__bridge id<MTLCommandQueue>)m_commandQueue;
        id<MTLCommandBuffer> cb = [cq commandBuffer];
        [cb commit];
        [cb waitUntilCompleted];
    }
}

std::shared_ptr<ITexture> MetalRenderDevice::bindExternalHardwareBuffer(
    void* /*nativeBuffer*/)
{
    return nullptr; // CVPixelBuffer binding: use createTextureFromHandle instead
}

// ---------------------------------------------------------------------------
// Shader program
// ---------------------------------------------------------------------------
std::shared_ptr<IShaderProgram> MetalRenderDevice::createShaderProgram(
    const char* vertexSrc, const char* fragmentSrc)
{
    id<MTLDevice> dev = (__bridge id<MTLDevice>)m_device;

    if (!dev) return nullptr;
    // Prefer pre-compiled library; fall back to runtime source compilation.
    id<MTLLibrary> lib = m_library ? (__bridge id<MTLLibrary>)m_library : nil;

    if (!lib && vertexSrc && fragmentSrc) {
        NSString* src = [NSString stringWithFormat:@"%s\n%s", vertexSrc, fragmentSrc];
        NSError* err = nil;
        lib = [dev newLibraryWithSource:src options:nil error:&err];
        if (!lib) {
            LOGE("Metal shader compile failed: %s",
                 [[err localizedDescription] UTF8String]);
            return nullptr;
        }
    }

    return std::make_shared<MetalShaderProgram>(
        (__bridge_retained void*)lib, m_commandQueue,
        vertexSrc ? vertexSrc : "",
        fragmentSrc ? fragmentSrc : "");
}

// ---------------------------------------------------------------------------
// getCapabilities
// ---------------------------------------------------------------------------
RHICapabilities MetalRenderDevice::getCapabilities() const {
    RHICapabilities caps;
    caps.backend          = BackendType::METAL;
    caps.computeShader    = true;  // Metal supports compute on all Apple GPUs
    caps.geometryShader   = false; // Metal does not expose geometry shaders
    caps.tessellation     = false; // Metal uses compute-based tessellation
    caps.msaa             = true;
    caps.maxMSAASamples   = 8;     // All Apple GPUs support ≥4x MSAA
    caps.fp16RenderTarget = true;  // MTLPixelFormatRGBA16Float always available
    caps.astc             = true;  // Mandatory on all Apple GPUs
    caps.glesVersionInt   = 0;     // N/A for Metal
    if (m_device) {
        id<MTLDevice> dev = (__bridge id<MTLDevice>)m_device;
        caps.rendererString = [[dev name] UTF8String];
    }
    return caps;
}

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // __APPLE__
