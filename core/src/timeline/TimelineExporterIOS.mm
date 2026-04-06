#include "../../include/timeline/TimelineExporter.h"

#ifdef __APPLE__
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#include <thread>
#include <atomic>

namespace sdk {
namespace video {
namespace timeline {

class TimelineExporterIOS : public TimelineExporter {
public:
    TimelineExporterIOS() : m_width(0), m_height(0), m_fps(30), m_bitrate(0), m_canceled(false) {}

    ~TimelineExporterIOS() override {
        cancel();
        if (m_exportThread.joinable()) {
            m_exportThread.join();
        }
    }

    Result configure(const std::string& outputPath, int width, int height, int fps, int bitrate) override {
        m_outputPath = outputPath;
        m_width = width;
        m_height = height;
        m_fps = fps;
        m_bitrate = bitrate;
        return Result::ok();
    }

    void exportAsync(std::shared_ptr<Timeline> timeline,
                     std::shared_ptr<Compositor> compositor,
                     ProgressCallback onProgress,
                     CompletionCallback onComplete) override {

        m_canceled = false;
        m_exportThread = std::thread([this, timeline, compositor, onProgress, onComplete]() {
            Result res = runExport(timeline, compositor, onProgress);
            if (onComplete) {
                onComplete(res);
            }
        });
    }

    void cancel() override {
        m_canceled = true;
    }

private:
    Result runExport(std::shared_ptr<Timeline> timeline,
                     std::shared_ptr<Compositor> compositor,
                     ProgressCallback onProgress) {

        if (!timeline || !compositor) return Result::error(-5001, "Invalid timeline or compositor");

        NSString* path = [NSString stringWithUTF8String:m_outputPath.c_str()];
        NSURL* url = [NSURL fileURLWithPath:path];

        if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
            [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
        }

        NSError* error = nil;
        AVAssetWriter* assetWriter = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&error];
        if (error || !assetWriter) {
            return Result::error(-5002, "Failed to create AVAssetWriter");
        }

        NSDictionary* videoSettings = @{
            AVVideoCodecKey: AVVideoCodecTypeH264,
            AVVideoWidthKey: @(m_width),
            AVVideoHeightKey: @(m_height),
            AVVideoCompressionPropertiesKey: @{
                AVVideoAverageBitRateKey: @(m_bitrate > 0 ? m_bitrate : 4000000)
            }
        };

        AVAssetWriterInput* videoInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
        videoInput.expectsMediaDataInRealTime = NO;

        NSDictionary* pixelBufferAttributes = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey: @(m_width),
            (id)kCVPixelBufferHeightKey: @(m_height),
            (id)kCVPixelBufferOpenGLESCompatibilityKey: @YES
        };

        AVAssetWriterInputPixelBufferAdaptor* adaptor =
            [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoInput
                                                                             sourcePixelBufferAttributes:pixelBufferAttributes];

        if ([assetWriter canAddInput:videoInput]) {
            [assetWriter addInput:videoInput];
        } else {
            return Result::error(-5003, "Cannot add video input");
        }

        // Setup background EAGL Context
        EAGLContext* context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        if (!context) {
            context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
        }
        if (!context) return Result::error(-5004, "Failed to create background EAGLContext");

        [EAGLContext setCurrentContext:context];

        CVOpenGLESTextureCacheRef textureCache = NULL;
        CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, context, NULL, &textureCache);
        if (err != kCVReturnSuccess) {
            return Result::error(-5005, "Failed to create texture cache");
        }

        [assetWriter startWriting];
        [assetWriter startSessionAtSourceTime:kCMTimeZero];

        int64_t totalDurationUs = timeline->getDuration();
        int64_t frameDurationUs = 1000000 / m_fps;
        int64_t currentTimeUs = 0;

        CVPixelBufferPoolRef pool = adaptor.pixelBufferPool;

        // FBO setup for offscreen rendering
        GLuint fbo;
        glGenFramebuffers(1, &fbo);

        while (currentTimeUs <= totalDurationUs && !m_canceled) {
            @autoreleasepool {
                while (!videoInput.readyForMoreMediaData && !m_canceled) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                if (m_canceled) break;

                CVPixelBufferRef pixelBuffer = NULL;
                err = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &pixelBuffer);
                if (err != kCVReturnSuccess || !pixelBuffer) {
                    break;
                }

                CVOpenGLESTextureRef cvTexture = NULL;
                err = CVOpenGLESTextureCacheCreateTextureFromImage(
                    kCFAllocatorDefault, textureCache, pixelBuffer, NULL,
                    GL_TEXTURE_2D, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height,
                    GL_BGRA, GL_UNSIGNED_BYTE, 0, &cvTexture);

                if (err == kCVReturnSuccess && cvTexture) {
                    GLuint textureId = CVOpenGLESTextureGetName(cvTexture);

                    // Render using compositor directly to the CVPixelBuffer-backed FBO
                    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
                    glViewport(0, 0, m_width, m_height);

                    // We create a temporary FrameBuffer wrapper for the Compositor
                    FrameBufferPtr fbWrapper = std::make_shared<FrameBuffer>(m_width, m_height);
                    // Hack: Manually assign the FBO/Texture to the wrapper to force Compositor to draw here
                    // In a clean architecture, Compositor renderFrameAtTime should take an FBO id directly, or we let it render to its own and then Blit.
                    // For safety and zero-copy, let's let Compositor render to its own FBO, then we Blit to ours.

                    FrameBufferPtr compOutputFb = std::make_shared<FrameBuffer>(m_width, m_height);

                    compositor->renderFrameAtTime(currentTimeUs, compOutputFb);

                    // Blit from compOutputFb to our CVPixelBuffer FBO
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, compOutputFb->getFboId());
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
                    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glFinish(); // Ensure GL commands complete before giving buffer to encoder

                    CMTime presentationTime = CMTimeMake(currentTimeUs, 1000000);
                    [adaptor appendPixelBuffer:pixelBuffer withPresentationTime:presentationTime];

                    CFRelease(cvTexture);
                    CVOpenGLESTextureCacheFlush(textureCache, 0);
                }

                CVPixelBufferRelease(pixelBuffer);

                if (onProgress) {
                    onProgress(static_cast<float>(currentTimeUs) / static_cast<float>(totalDurationUs));
                }

                currentTimeUs += frameDurationUs;
            }
        }

        [videoInput markAsFinished];

        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        [assetWriter finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(semaphore);
        }];
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

        glDeleteFramebuffers(1, &fbo);
        if (textureCache) CFRelease(textureCache);
        [EAGLContext setCurrentContext:nil];

        if (m_canceled) {
            return Result::error(-5006, "Export canceled by user");
        }

        return Result::ok();
    }

    std::string m_outputPath;
    int m_width;
    int m_height;
    int m_fps;
    int m_bitrate;

    std::atomic<bool> m_canceled;
    std::thread m_exportThread;
};

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // __APPLE__
