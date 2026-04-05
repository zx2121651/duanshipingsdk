#include "../../include/timeline/VideoDecoder.h"

#ifdef __APPLE__
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

namespace sdk {
namespace video {
namespace timeline {

class VideoDecoderIOS : public VideoDecoder {
public:
    VideoDecoderIOS() : m_assetReader(nullptr), m_trackOutput(nullptr), m_textureCache(nullptr), m_cvTexture(nullptr), m_width(0), m_height(0) {}

    ~VideoDecoderIOS() override {
        close();
    }

    Result open(const std::string& filePath) override {
        NSString* path = [NSString stringWithUTF8String:filePath.c_str()];
        NSURL* url = [NSURL fileURLWithPath:path];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];

        NSError* error = nil;
        m_assetReader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (error) {
            return Result::error(-4001, "Failed to create AVAssetReader");
        }

        NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
        if (tracks.count == 0) {
            return Result::error(-4003, "No video track found");
        }
        AVAssetTrack* videoTrack = tracks.firstObject;

        CGSize size = videoTrack.naturalSize;
        m_width = size.width;
        m_height = size.height;

        NSDictionary* outputSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };

        m_trackOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack outputSettings:outputSettings];
        m_trackOutput.alwaysCopiesSampleData = NO;

        if ([m_assetReader canAddOutput:m_trackOutput]) {
            [m_assetReader addOutput:m_trackOutput];
        } else {
            return Result::error(-4004, "Cannot add track output");
        }

        EAGLContext* context = [EAGLContext currentContext];
        if (!context) {
            return Result::error(-4005, "No EAGLContext available");
        }

        CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, context, NULL, &m_textureCache);
        if (err != kCVReturnSuccess) {
            return Result::error(-4006, "Failed to create texture cache");
        }

        [m_assetReader startReading];

        return Result::ok();
    }

    Texture getFrameAt(int64_t timeUs) override {
        if (!m_assetReader || !m_trackOutput) return {0, 0, 0};

        // Note: AVAssetReader does not support fast seeking dynamically.
        // For sync NLE seek, we'd typically recreate the reader with timeRange.
        // In a true production environment, AVPlayerItemVideoOutput is better for dynamic playback,
        // while AVAssetReader is better for sequential export. We'll simulate reading next frame here.

        CMSampleBufferRef sampleBuffer = [m_trackOutput copyNextSampleBuffer];
        if (sampleBuffer) {
            CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (pixelBuffer) {
                if (m_cvTexture) {
                    CFRelease(m_cvTexture);
                    m_cvTexture = nullptr;
                }

                CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(
                    kCFAllocatorDefault,
                    m_textureCache,
                    pixelBuffer,
                    NULL,
                    GL_TEXTURE_2D,
                    GL_RGBA,
                    (GLsizei)m_width,
                    (GLsizei)m_height,
                    GL_BGRA,
                    GL_UNSIGNED_BYTE,
                    0,
                    &m_cvTexture
                );

                if (err == kCVReturnSuccess && m_cvTexture) {
                    GLuint textureId = CVOpenGLESTextureGetName(m_cvTexture);
                    CMSampleBufferInvalidate(sampleBuffer);
                    CFRelease(sampleBuffer);
                    return {textureId, (uint32_t)m_width, (uint32_t)m_height};
                }
            }
            CMSampleBufferInvalidate(sampleBuffer);
            CFRelease(sampleBuffer);
        }

        return {0, 0, 0};
    }

    void close() override {
        if (m_assetReader) {
            [m_assetReader cancelReading];
            m_assetReader = nil;
        }
        m_trackOutput = nil;

        if (m_cvTexture) {
            CFRelease(m_cvTexture);
            m_cvTexture = nullptr;
        }

        if (m_textureCache) {
            CFRelease(m_textureCache);
            m_textureCache = nullptr;
        }
    }

private:
    AVAssetReader* m_assetReader;
    AVAssetReaderTrackOutput* m_trackOutput;
    CVOpenGLESTextureCacheRef m_textureCache;
    CVOpenGLESTextureRef m_cvTexture;
    int32_t m_width;
    int32_t m_height;
};

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // __APPLE__
