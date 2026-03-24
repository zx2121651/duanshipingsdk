#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, FilterType) {
    FilterTypeBrightness = 0,
    FilterTypeGaussianBlur = 1,
    FilterTypeLookup = 2
};

@interface FilterEngineWrapper : NSObject

- (instancetype)init;

- (void)initializeWithContext:(EAGLContext *)context;

// Uses CVOpenGLESTextureCache for fast path mapping of CVPixelBuffer to OpenGL texture
// Returns a new CVPixelBuffer containing the filtered result.
- (nullable CVPixelBufferRef)processFrame:(CVPixelBufferRef)pixelBuffer;

// Updates a floating-point parameter
- (void)updateParameterFloat:(NSString *)key value:(float)value;

// Updates an integer parameter (e.g., texture ID for LUT)
- (void)updateParameterInt:(NSString *)key value:(int)value;

- (void)addFilter:(FilterType)type;
- (void)removeAllFilters;

- (void)releaseResources;

@end

NS_ASSUME_NONNULL_END
