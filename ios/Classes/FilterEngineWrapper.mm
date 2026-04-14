#import "FilterEngineWrapper.h"
#import "../../core/include/FilterEngine.h"
#import "IOSAssetProvider.h"
#import "../../core/include/Filters.h"
#import <OpenGLES/ES3/glext.h>
#import <OpenGLES/ES2/glext.h>

using namespace sdk::video;

#import <CoreVideo/CVOpenGLESTextureCache.h>

@interface FilterEngineWrapper () {
    std::shared_ptr<FilterEngine> engine;
    CVOpenGLESTextureCacheRef textureCache;
    CVPixelBufferPoolRef pixelBufferPool;
    size_t poolWidth;
    size_t poolHeight;
    GLuint blitFboRead;
    GLuint blitFboDraw;
}
@end

@implementation FilterEngineWrapper

- (instancetype)init {
    self = [super init];
    if (self) {
        engine = std::make_shared<FilterEngine>();
        engine->setAssetProvider(std::make_shared<IOSAssetProvider>());
        textureCache = NULL;
        pixelBufferPool = NULL;
        poolWidth = 0;
        poolHeight = 0;
        blitFboRead = 0;
        blitFboDraw = 0;
    }
    return self;
}

- (int)initializeWithContext:(EAGLContext *)context {
    if (!context || !engine) return static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);

    [EAGLContext setCurrentContext:context];

    CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, context, NULL, &textureCache);
    if (err != kCVReturnSuccess) {
        return static_cast<int>(sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED); // Texture cache creation failed
    }

    Result res = engine->initialize();
    if (!res.isOk()) {
        return res.getErrorCode() != 0 ? res.getErrorCode() : -2;
    }
    return 0; // OK
}

- (CVPixelBufferRef)processFrame:(CVPixelBufferRef)pixelBuffer {
    if (!engine || !textureCache || !pixelBuffer) return pixelBuffer;

    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);

    // Map the incoming CoreVideo buffer to an OpenGL texture
    CVOpenGLESTextureRef cvTexture = NULL;
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
        return pixelBuffer;
    }

    GLuint inputTextureId = CVOpenGLESTextureGetName(cvTexture);

    // Process the frame via C++ core engine
    Texture inTex = {inputTextureId, (uint32_t)width, (uint32_t)height};
    auto result = engine->processFrame(inTex, (int)width, (int)height);
    if (!result.isOk()) {
        NSLog(@"FilterEngineWrapper: processFrame failed [%d] %s", result.getErrorCode(), result.getMessage().c_str());
        CVBufferRelease(cvTexture);
        return nil; // Return nil so Swift bypasses or emits fallback
    }

    Texture outTex = result.getValue();

    // Zero-copy output: Create a CVPixelBuffer backed texture, and blit the result to it
    if (poolWidth != width || poolHeight != height || !pixelBufferPool) {
        if (pixelBufferPool) {
            CVPixelBufferPoolRelease(pixelBufferPool);
        }
        NSDictionary *pixelBufferAttributes = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey: @(width),
            (id)kCVPixelBufferHeightKey: @(height),
            (id)kCVPixelBufferOpenGLESCompatibilityKey: @(YES),
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
        };
        CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL, (__bridge CFDictionaryRef)pixelBufferAttributes, &pixelBufferPool);
        poolWidth = width;
        poolHeight = height;
    }

    CVPixelBufferRef outPixelBuffer = NULL;
    err = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pixelBufferPool, &outPixelBuffer);
    if (err != kCVReturnSuccess || !outPixelBuffer) {
        CFRelease(cvTexture);
        return pixelBuffer;
    }

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
        CVPixelBufferRelease(outPixelBuffer);
        CFRelease(cvTexture);
        return pixelBuffer;
    }

    GLuint outputTextureId = CVOpenGLESTextureGetName(outCvTexture);

    // Create or bind FBOs for blitting
    if (blitFboRead == 0) {
        glGenFramebuffers(1, &blitFboRead);
    }
    if (blitFboDraw == 0) {
        glGenFramebuffers(1, &blitFboDraw);
    }

    GLint oldReadFBO, oldDrawFBO;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDrawFBO);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, blitFboRead);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex.id, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blitFboDraw);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTextureId, 0);

    // Blit from engine output to CVPixelBuffer-backed texture
    glBlitFramebuffer(0, 0, (GLint)width, (GLint)height, 0, 0, (GLint)width, (GLint)height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Restore state
    glBindFramebuffer(GL_READ_FRAMEBUFFER, oldReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDrawFBO);

    // Cleanup CV resources for this frame
    CFRelease(outCvTexture);
    CFRelease(cvTexture);
    CVOpenGLESTextureCacheFlush(textureCache, 0);

    // Return the new zero-copy buffer
    return outPixelBuffer;
}

- (int)addFilter:(IOSFilterType)type {
    if (!engine) return static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
    auto res = engine->addFilter(static_cast<sdk::video::FilterType>(type));
    return res.isOk() ? static_cast<int>(sdk::video::ErrorCode::SUCCESS) : res.getErrorCode();
}

- (int)removeAllFilters {
    if (!engine) return static_cast<int>(sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED);
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
    if (textureCache) {
        CFRelease(textureCache);
        textureCache = NULL;
    }
    if (pixelBufferPool) {
        CVPixelBufferPoolRelease(pixelBufferPool);
        pixelBufferPool = NULL;
    }
    if (blitFboRead != 0) {
        glDeleteFramebuffers(1, &blitFboRead);
        blitFboRead = 0;
    }
    if (blitFboDraw != 0) {
        glDeleteFramebuffers(1, &blitFboDraw);
        blitFboDraw = 0;
    }
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

@end
