#import "FilterEngineWrapper.h"
#import "../../../core/include/FilterEngine.h"
#import "../../../core/include/Filters.h"

using namespace sdk::video;

@interface FilterEngineWrapper () {
    std::shared_ptr<FilterEngine> engine;
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
    if (!context || !engine) return -1;

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
    if (!engine) return -1;

    FilterPtr filter;
    switch(type) {
        case FilterTypeBrightness: filter = std::make_shared<BrightnessFilter>(); break;
        case FilterTypeGaussianBlur: filter = std::make_shared<GaussianBlurFilter>(&(self->engine->m_frameBufferPool)); break;
        case FilterTypeLookup: filter = std::make_shared<LookupFilter>(); break;
        case FilterTypeBilateral: filter = std::make_shared<BilateralFilter>(); break;
        case FilterTypeCinematicLookup: filter = std::make_shared<CinematicLookupFilter>(); break;
        default: return -3; // ComputeBlurFilter is not supported on iOS (GLES 3.1)
    }

    if (filter) {
        engine->addFilter(filter);
        return 0;
    }
    return -2;
}

- (int)removeAllFilters {
    if (!engine) return -1;
    engine->removeAllFilters();
    return 0;
}

- (int)updateParameterFloat:(NSString *)key value:(float)value {
    if (!engine || !key) return -1;
    engine->updateParameter([key UTF8String], std::any(value));
    return 0;
}

- (int)updateParameterInt:(NSString *)key value:(int32_t)value {
    if (!engine || !key) return -1;
    engine->updateParameter([key UTF8String], std::any(value));
    return 0;
}

- (void)releaseResources {
    if (engine) {
        engine->release();
    }
}

@end
