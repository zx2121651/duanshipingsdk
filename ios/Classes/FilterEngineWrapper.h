#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGLES/EAGL.h>

typedef NS_ENUM(NSInteger, FilterType) {
    FilterTypeBrightness = 0,
    FilterTypeGaussianBlur = 1,
    FilterTypeLookup = 2,
    FilterTypeBilateral = 3,
    FilterTypeCinematicLookup = 4,
    FilterTypeComputeBlur = 5
};

@interface FilterEngineWrapper : NSObject

- (instancetype)init;
- (int)initializeWithContext:(EAGLContext *)context;
- (CVPixelBufferRef)processFrame:(CVPixelBufferRef)pixelBuffer;

- (int)addFilter:(FilterType)type;
- (int)removeAllFilters;
- (int)updateParameterFloat:(NSString *)key value:(float)value;
- (int)updateParameterInt:(NSString *)key value:(int32_t)value;


- (NSArray<NSNumber *> *)getPerformanceMetrics;
- (void)recordDroppedFrame;
- (void)releaseResources;

@end
