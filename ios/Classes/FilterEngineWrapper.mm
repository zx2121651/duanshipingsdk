#import "FilterEngineWrapper.h"
#import "../../../core/include/FilterEngine.h"
#import "../../../core/include/Filters.h"

using namespace sdk::video;


#import <CoreVideo/CVOpenGLESTextureCache.h>

@interface FilterEngineWrapper () {
    std::shared_ptr<FilterEngine> engine;
    CVOpenGLESTextureCacheRef textureCache;
}
@end

@implementation FilterEngineWrapper

- (instancetype)init {
    self = [super init];
    if (self) {
        engine = std::make_shared<FilterEngine>();
        textureCache = NULL;
    }
    return self;
}

- (int)initializeWithContext:(EAGLContext *)context {
    if (!context || !engine) return sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED;

    [EAGLContext setCurrentContext:context];

    CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, context, NULL, &textureCache);
    if (err != kCVReturnSuccess) {
        return sdk::video::ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED; // Texture cache creation failed
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
                                                                GL_BGRA,
                                                                GL_UNSIGNED_BYTE,
                                                                0,
                                                                &cvTexture);

    if (err != kCVReturnSuccess || !cvTexture) {
        return pixelBuffer;
    }

    GLuint inputTextureId = CVOpenGLESTextureGetName(cvTexture);

    // Process the frame via C++ core engine
    Texture inTex = {inputTextureId, (uint32_t)width, (uint32_t)height};
    Texture outTex = engine->processFrame(inTex, (int)width, (int)height);

    // For true zero-copy processing back to AVFoundation, we should ideally render into a new CVPixelBuffer
    // backed FBO. For now, since outTex holds the FBO ID, if we want the output buffer as CVPixelBuffer,
    // we would create an output CVPixelBuffer and attach an FBO to it.
    // Given the complexity of providing a full output pool here, we'll return the input buffer with effects applied
    // if we rendered directly to it (not possible with read-only buffers).
    // For the sake of this architectural fix, we will just release the CV texture and return.

    CFRelease(cvTexture);
    CVOpenGLESTextureCacheFlush(textureCache, 0);

    // In a production pipeline, outTex would be read via glReadPixels or bound to an output CVPixelBuffer.
    return pixelBuffer; // TODO: Implement zero-copy output CVMetalTextureCache map
}
@end

@implementation FilterEngineWrapper

- (instancetype)init {
    self = [super init];
    if (self) {
        engine = std::make_shared<FilterEngine>();
    }
    return self;
}

- (int)initializeWithContext:(EAGLContext *)context {
    if (!context || !engine) return sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED;

    [EAGLContext setCurrentContext:context];

    // We expect engine->initialize() to return Result, let's map it
    Result res = engine->initialize();
    if (!res.isOk()) {
        return res.getErrorCode() != 0 ? res.getErrorCode() : -2;
    }
    return 0; // OK
}

- (CVPixelBufferRef)processFrame:(CVPixelBufferRef)pixelBuffer {
    if (!engine) return pixelBuffer;
    // For iOS, texture handling mapping needs CVMetalTextureCache or similar.
    // Placeholder passing back input buffer as NLE N/A fallback for now
    // Texture outTex = engine->processFrame(...);
    return pixelBuffer;
}

- (int)addFilter:(FilterType)type {
    if (!engine) return sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED;

    FilterPtr filter;
    switch(type) {
        case FilterTypeBrightness: filter = std::make_shared<BrightnessFilter>(); break;
        case FilterTypeGaussianBlur: filter = std::make_shared<GaussianBlurFilter>(&(self->engine->m_frameBufferPool)); break;
        case FilterTypeLookup: filter = std::make_shared<LookupFilter>(); break;
        case FilterTypeBilateral: filter = std::make_shared<BilateralFilter>(); break;
        case FilterTypeCinematicLookup: filter = std::make_shared<CinematicLookupFilter>(); break;
        default: return sdk::video::ErrorCode::ERR_RENDER_COMPUTE_NOT_SUPPORTED; // ComputeBlurFilter is not supported on iOS (GLES 3.1)
    }

    if (filter) {
        engine->addFilter(filter);
        return 0;
    }
    return sdk::video::ErrorCode::ERR_RENDER_INVALID_STATE;
}

- (int)removeAllFilters {
    if (!engine) return sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED;
    engine->removeAllFilters();
    return 0;
}

- (int)updateParameterFloat:(NSString *)key value:(float)value {
    if (!engine || !key) return sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED;
    engine->updateParameter([key UTF8String], std::any(value));
    return 0;
}

- (int)updateParameterInt:(NSString *)key value:(int32_t)value {
    if (!engine || !key) return sdk::video::ErrorCode::ERR_INIT_CONTEXT_FAILED;
    engine->updateParameter([key UTF8String], std::any(value));
    return 0;
}

- (void)releaseResources {
    if (engine) {
        engine->release();
    }
}

@end
