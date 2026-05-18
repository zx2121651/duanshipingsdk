#import "FilterEngineWrapper.h"
#import "../../core/include/FilterEngine.h"
#import "IOSAssetProvider.h"
#import "../../core/include/Filters.h"
#import <OpenGLES/ES3/glext.h>
#import <OpenGLES/ES2/glext.h>

using namespace sdk::video;

#ifdef HAS_METAL
#import <Metal/Metal.h>
#import <CoreVideo/CVMetalTextureCache.h>
#include "../../core/src/rhi/mtl/MetalRenderDevice.h"
#endif

#import <CoreVideo/CVOpenGLESTextureCache.h>

@interface FilterEngineWrapper () {
    std::shared_ptr<FilterEngine> engine;
    EAGLContext *m_context;
    CVOpenGLESTextureCacheRef textureCache;
    CVPixelBufferPoolRef pixelBufferPool;
    size_t poolWidth;
    size_t poolHeight;
    GLuint blitFboRead;
    GLuint blitFboDraw;
#ifdef HAS_METAL
    CVMetalTextureCacheRef metalTextureCache;
#endif
}
@property (nonatomic, readwrite) int lastErrorCode;
@end

@implementation FilterEngineWrapper

- (instancetype)init {
    self = [super init];
    if (self) {
        engine = std::make_shared<FilterEngine>();
        engine->setAssetProvider(std::make_shared<IOSAssetProvider>());
        m_context = nil;
        textureCache = NULL;
        pixelBufferPool = NULL;
        poolWidth = 0;
        poolHeight = 0;
        poolHeight = 0;
        blitFboRead = 0;
        blitFboDraw = 0;
#ifdef HAS_METAL
        metalTextureCache = NULL;
#endif
        _lastErrorCode = 0;
    }
    return self;
}

- (void)_cleanupGLResources {
    if (m_context) {
        [EAGLContext setCurrentContext:m_context];
    }
    if (textureCache) {
        CFRelease(textureCache);
        textureCache = NULL;
    }
    if (pixelBufferPool) {
        CVPixelBufferPoolRelease(pixelBufferPool);
        pixelBufferPool = NULL;
        poolWidth = 0;
        poolHeight = 0;
    }
    if (blitFboRead != 0) {
        glDeleteFramebuffers(1, &blitFboRead);
        blitFboRead = 0;
    }
    if (blitFboDraw != 0) {
        glDeleteFramebuffers(1, &blitFboDraw);
        blitFboDraw = 0;
    }
#ifdef HAS_METAL
    if (metalTextureCache) {
        CFRelease(metalTextureCache);
        metalTextureCache = NULL;
    }
#endif
}

- (int)initializeWithContext:(EAGLContext *)context backend:(int)backend {
    if (!engine) {
        self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
        return self.lastErrorCode;
    }

    if (m_context || metalTextureCache) {
        [self _cleanupGLResources];
    }

    engine->setPreferredBackend(static_cast<sdk::video::rhi::BackendType>(backend));

    if (backend == static_cast<int>(sdk::video::rhi::BackendType::METAL) ||
        backend == static_cast<int>(sdk::video::rhi::BackendType::AUTO)) {
        // We will let the engine try to initialize Metal first.
        // We won't strictly enforce context existence for Metal.
    } else {
        if (!context) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
            return self.lastErrorCode;
        }
        m_context = context;
        [EAGLContext setCurrentContext:m_context];
    }

    Result res = engine->initialize();
    if (!res.isOk()) {
        self.lastErrorCode = res.getErrorCode() != 0 ? res.getErrorCode() : -2;
        return self.lastErrorCode;
    }

    // Now check what backend was actually selected
    if (engine->getActiveBackend() == sdk::video::rhi::BackendType::METAL) {
#ifdef HAS_METAL
        sdk::video::rhi::MetalRenderDevice* mtlDevice = static_cast<sdk::video::rhi::MetalRenderDevice*>(engine->getRenderDevice());
        if (mtlDevice && mtlDevice->mtlDevice()) {
            CVReturn err = CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, (__bridge id<MTLDevice>)mtlDevice->mtlDevice(), NULL, &metalTextureCache);
            if (err != kCVReturnSuccess) {
                self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED);
                return self.lastErrorCode;
            }
        }
#endif
    } else {
        // Fallback or explicit GLES
        if (!context) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
            return self.lastErrorCode;
        }
        m_context = context;
        [EAGLContext setCurrentContext:m_context];
        CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, m_context, NULL, &textureCache);
        if (err != kCVReturnSuccess) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED);
            return self.lastErrorCode;
        }
    }

    self.lastErrorCode = 0;
    return 0; // OK
}

- (CVPixelBufferRef)processFrame:(CVPixelBufferRef)pixelBuffer {
    if (!engine || !pixelBuffer) {
        self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_INVALID_STATE);
        return nil;
    }

    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);

    sdk::video::Texture inTex;
    GLuint inputTextureId = 0;
    CVOpenGLESTextureRef cvTexture = NULL;
#ifdef HAS_METAL
    CVMetalTextureRef metalTexture = NULL;
#endif

    bool isMetal = (engine->getActiveBackend() == sdk::video::rhi::BackendType::METAL);

    if (isMetal) {
#ifdef HAS_METAL
        if (!metalTextureCache) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_INVALID_STATE);
            return nil;
        }
        CVReturn err = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                 metalTextureCache,
                                                                 pixelBuffer,
                                                                 NULL,
                                                                 MTLPixelFormatBGRA8Unorm,
                                                                 width, height, 0, &metalTexture);
        if (err != kCVReturnSuccess || !metalTexture) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED);
            return nil;
        }
        id<MTLTexture> mtlTex = CVMetalTextureGetTexture(metalTexture);
        inTex = { (uint32_t)(uintptr_t)(__bridge void*)mtlTex, (uint32_t)width, (uint32_t)height };
#endif
    } else {
        if (!m_context || !textureCache) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_INVALID_STATE);
            return nil;
        }
        [EAGLContext setCurrentContext:m_context];
        CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                    textureCache,
                                                                    pixelBuffer,
                                                                    NULL,
                                                                    GL_TEXTURE_2D,
                                                                    GL_RGBA,
                                                                    (GLsizei)width,
                                                                    (GLsizei)height,
                                                                    GL_BGRA_EXT,
                                                                    GL_UNSIGNED_BYTE,
                                                                    0,
                                                                    &cvTexture);

        if (err != kCVReturnSuccess || !cvTexture) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED);
            return nil;
        }
        inputTextureId = CVOpenGLESTextureGetName(cvTexture);
        inTex = { inputTextureId, (uint32_t)width, (uint32_t)height };
    }

    // Process the frame via C++ core engine
    auto result = engine->processFrame(inTex, (int)width, (int)height);
    if (!result.isOk()) {
        NSLog(@"FilterEngineWrapper: processFrame failed [%d] %s", result.getErrorCode(), result.getMessage().c_str());
        self.lastErrorCode = result.getErrorCode();
#ifdef HAS_METAL
        if (metalTexture) CFRelease(metalTexture);
#endif
        if (cvTexture) CFRelease(cvTexture);
        return nil;
    }

    sdk::video::Texture outTex = result.getValue();

    // Zero-copy output: Create a CVPixelBuffer backed texture, and blit the result to it
    if (poolWidth != width || poolHeight != height || !pixelBufferPool) {
        if (pixelBufferPool) {
            CVPixelBufferPoolRelease(pixelBufferPool);
            pixelBufferPool = NULL;
        }
        NSDictionary *pixelBufferAttributes;
        if (isMetal) {
            pixelBufferAttributes = @{
                (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                (id)kCVPixelBufferWidthKey: @(width),
                (id)kCVPixelBufferHeightKey: @(height),
                (id)kCVPixelBufferMetalCompatibilityKey: @(YES),
                (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
            };
        } else {
            pixelBufferAttributes = @{
                (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                (id)kCVPixelBufferWidthKey: @(width),
                (id)kCVPixelBufferHeightKey: @(height),
                (id)kCVPixelBufferOpenGLESCompatibilityKey: @(YES),
                (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
            };
        }
        CVReturn err = CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL, (__bridge CFDictionaryRef)pixelBufferAttributes, &pixelBufferPool);
        if (err != kCVReturnSuccess || !pixelBufferPool) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED);
#ifdef HAS_METAL
            if (metalTexture) CFRelease(metalTexture);
#endif
            if (cvTexture) CFRelease(cvTexture);
            return nil;
        }
        poolWidth = width;
        poolHeight = height;
    }

    CVPixelBufferRef outPixelBuffer = NULL;
    CVReturn err = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pixelBufferPool, &outPixelBuffer);
    if (err != kCVReturnSuccess || !outPixelBuffer) {
        self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED);
#ifdef HAS_METAL
        if (metalTexture) CFRelease(metalTexture);
#endif
        if (cvTexture) CFRelease(cvTexture);
        return nil;
    }

    if (isMetal) {
#ifdef HAS_METAL
        CVMetalTextureRef outMetalTexture = NULL;
        err = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                        metalTextureCache,
                                                        outPixelBuffer,
                                                        NULL,
                                                        MTLPixelFormatBGRA8Unorm,
                                                        width, height, 0, &outMetalTexture);
        if (err != kCVReturnSuccess || !outMetalTexture) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED);
            CVPixelBufferRelease(outPixelBuffer);
            if (metalTexture) CFRelease(metalTexture);
            return nil;
        }

        id<MTLTexture> mtlOutTex = CVMetalTextureGetTexture(outMetalTexture);
        id<MTLTexture> mtlEngineOutTex = (__bridge id<MTLTexture>)(void*)(uintptr_t)outTex.id;

        sdk::video::rhi::MetalRenderDevice* mtlDevice = static_cast<sdk::video::rhi::MetalRenderDevice*>(engine->getRenderDevice());
        id<MTLCommandBuffer> commandBuffer = [mtlDevice->mtlCommandQueue() commandBuffer];
        id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];

        [blitEncoder copyFromTexture:mtlEngineOutTex
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(width, height, 1)
                           toTexture:mtlOutTex
                    destinationSlice:0
                    destinationLevel:0
                   destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blitEncoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        CFRelease(outMetalTexture);
        CFRelease(metalTexture);
        CVMetalTextureCacheFlush(metalTextureCache, 0);
#endif
    } else {
        CVOpenGLESTextureRef outCvTexture = NULL;
        err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                           textureCache,
                                                           outPixelBuffer,
                                                           NULL,
                                                           GL_TEXTURE_2D,
                                                           GL_RGBA,
                                                           (GLsizei)width,
                                                           (GLsizei)height,
                                                           GL_BGRA_EXT,
                                                           GL_UNSIGNED_BYTE,
                                                           0,
                                                           &outCvTexture);
        if (err != kCVReturnSuccess || !outCvTexture) {
            self.lastErrorCode = static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED);
            CVPixelBufferRelease(outPixelBuffer);
            CFRelease(cvTexture);
            return nil;
        }

        GLuint outputTextureId = CVOpenGLESTextureGetName(outCvTexture);

        // Create or bind FBOs for blitting
        if (blitFboRead == 0) glGenFramebuffers(1, &blitFboRead);
        if (blitFboDraw == 0) glGenFramebuffers(1, &blitFboDraw);

        GLint oldReadFBO, oldDrawFBO;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldReadFBO);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDrawFBO);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, blitFboRead);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex.id, 0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blitFboDraw);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTextureId, 0);

        // Blit from engine output to CVPixelBuffer-backed texture
        glBlitFramebuffer(0, 0, (GLint)width, (GLint)height, 0, 0, (GLint)width, (GLint)height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // Detach textures to avoid illegal reuse or holding onto references
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

        // Restore state
        glBindFramebuffer(GL_READ_FRAMEBUFFER, oldReadFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDrawFBO);

        // Cleanup CV resources for this frame
        CFRelease(outCvTexture);
        CFRelease(cvTexture);
        CVOpenGLESTextureCacheFlush(textureCache, 0);
    }

    // Return the new zero-copy buffer (Ownership is transferred to caller)
    self.lastErrorCode = 0;
    return outPixelBuffer;
}

- (int)addFilter:(IOSFilterType)type {
    if (!engine) return static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
    if (m_context) [EAGLContext setCurrentContext:m_context];
    auto res = engine->addFilter(static_cast<sdk::video::FilterType>(type));
    return res.isOk() ? static_cast<int>(sdk::video::ErrorCode::SUCCESS) : res.getErrorCode();
}

- (int)removeAllFilters {
    if (!engine) return static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
    if (m_context) [EAGLContext setCurrentContext:m_context];
    auto res = engine->removeAllFilters();
    return res.isOk() ? static_cast<int>(sdk::video::ErrorCode::SUCCESS) : res.getErrorCode();
}

- (int)updateParameterFloat:(NSString *)key value:(float)value {
    if (!engine || !key) return static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
    engine->updateParameter([key UTF8String], std::any(value));
    return 0;
}

- (int)updateParameterInt:(NSString *)key value:(int32_t)value {
    if (!engine || !key) return static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
    engine->updateParameter([key UTF8String], std::any(value));
    return 0;
}

- (void)releaseResources {
    if (engine) {
        engine->release();
    }
    [self _cleanupGLResources];
    m_context = nil;
}

- (NSArray<NSNumber *> *)getPerformanceMetrics {
    if (!engine) return nil;
    sdk::video::PerformanceMetrics metrics = engine->getPerformanceMetrics();
    return @[
        @(metrics.averageFrameTimeMs),
        @(metrics.p50FrameTimeMs),
        @(metrics.p90FrameTimeMs),
        @(metrics.p99FrameTimeMs),
        @(metrics.droppedFrames)
    ];
}

- (void)recordDroppedFrame {
    if (engine) {
        engine->recordDroppedFrame();
    }
}

- (void)dealloc {
    [self releaseResources];
}

@end
