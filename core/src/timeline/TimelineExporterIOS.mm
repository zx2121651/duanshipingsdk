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

/**
 * @Experimental
 * This is an experimental implementation.
 */
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

        int64_t totalDurationNs = timeline->getTotalDuration();
        int64_t frameDurationNs = 1000000000 / m_fps;
        int64_t currentTimeNs = 0;

        CVPixelBufferPoolRef pool = adaptor.pixelBufferPool;

        // FBO setup for offscreen rendering
        GLuint fbo;
        glGenFramebuffers(1, &fbo);

        FrameBufferPtr cvExternalFbWrapper = std::make_shared<FrameBuffer>(m_width, m_height, fbo);

        while (currentTimeNs <= totalDurationNs && !m_canceled) {
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

                    // [P1 修复] 统一 Compositor 输出接口，去掉 iOS 的二次拷贝 hack
                    // 使用扩展的 FrameBuffer 构造函数直接包装 CVPixelBuffer 挂载的外部 FBO。
                    // Compositor 最后一层的 Explicit Copy Pass 会直接画到这个 FBO 里，完成真正的零拷贝编码。
                    cvExternalFbWrapper->setExternalFboId(fbo); // Since fbo id is constant, this is optional, but keeps wrapper in sync if fbo ever changes

                    compositor->renderFrameAtTime(currentTimeNs, cvExternalFbWrapper);

                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glFinish(); // 确保渲染指令完成

                    CMTime presentationTime = CMTimeMake(currentTimeNs, 1000000000);
                    [adaptor appendPixelBuffer:pixelBuffer withPresentationTime:presentationTime];

                    CFRelease(cvTexture);
                    CVOpenGLESTextureCacheFlush(textureCache, 0);
                }

                CVPixelBufferRelease(pixelBuffer);

                if (onProgress) {
                    onProgress(static_cast<float>(currentTimeNs) / static_cast<float>(totalDurationNs));
                }

                currentTimeNs += frameDurationNs;
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
