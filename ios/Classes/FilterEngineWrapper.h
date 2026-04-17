#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGLES/EAGL.h>

typedef NS_ENUM(NSInteger, IOSFilterType) {
    IOSFilterTypeBrightness = 0,
    IOSFilterTypeGaussianBlur = 1,
    IOSFilterTypeLookup = 2,
    IOSFilterTypeBilateral = 3,
    IOSFilterTypeCinematicLookup = 4,
    IOSFilterTypeComputeBlur = 5,
    IOSFilterTypeNightVision = 6
};

@interface FilterEngineWrapper : NSObject

@property (nonatomic, readonly) int lastErrorCode;

- (instancetype)init;
- (int)initializeWithContext:(EAGLContext *)context;
- (CVPixelBufferRef)processFrame:(CVPixelBufferRef)pixelBuffer;

- (int)addFilter:(IOSFilterType)type;
- (int)removeAllFilters;
- (int)updateParameterFloat:(NSString *)key value:(float)value;
- (int)updateParameterInt:(NSString *)key value:(int32_t)value;


- (NSArray<NSNumber *> *)getPerformanceMetrics;
- (void)recordDroppedFrame;
- (void)releaseResources;

@end
