#include "../../include/timeline/TimelineExporter.h"

#ifdef __APPLE__
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGLES/EAGL.h>
#define LOG_TAG "TimelineExporterIOS"
#include "../../include/Log.h"
#import <OpenGLES/ES3/gl.h>
#include "../../include/GLStateManager.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

namespace sdk {
namespace video {
namespace timeline {

class TimelineExporterIOS : public TimelineExporter {
public:
    TimelineExporterIOS()
        : m_width(0), m_height(0), m_fps(30), m_bitrate(0),
          m_state(State::IDLE), m_progress(0.0f), m_chunkDurationNs(0) {}

    ~TimelineExporterIOS() override {
        cancel();
        if (m_exportThread.joinable()) {
            m_exportThread.join();
        }
    }

    Result configure(const std::string& outputPath, int width, int height, int fps, int bitrate) override {
        if (m_state != State::IDLE && m_state != State::COMPLETED && m_state != State::FAILED && m_state != State::CANCELED) {
            LOGE("Cannot configure while running, current state: %d", (int)m_state.load());
            return Result::error(ErrorCode::ERR_EXPORTER_ALREADY_RUNNING, "Cannot configure while running");
        }
        LOGI("Configuring exporter: path=%s, size=%dx%d, fps=%d, bitrate=%d", outputPath.c_str(), width, height, fps, bitrate);
        m_outputPath = outputPath;
        m_width = width;
        m_height = height;
        m_fps = fps;
        m_bitrate = bitrate;
        m_state = State::IDLE;
        m_progress = 0.0f;
        return Result::ok();
    }

    void configureChunking(int64_t chunkDurationNs, ChunkCallback onChunkReady) override {
        m_chunkDurationNs = chunkDurationNs;
        m_onChunkReady = onChunkReady;
    }

    Result exportAsync(std::shared_ptr<Timeline> timeline,
                     std::shared_ptr<Compositor> compositor,
                     ProgressCallback onProgress,
                     CompletionCallback onComplete) override {

        if (m_state == State::STARTING || m_state == State::EXPORTING) {
            LOGE("Export already in progress, current state: %d", (int)m_state.load());
            return Result::error(ErrorCode::ERR_EXPORTER_ALREADY_RUNNING, "Export already in progress");
        }

        if (m_outputPath.empty() || m_width <= 0 || m_height <= 0) {
            LOGE("Exporter not properly configured: path=%s, size=%dx%d", m_outputPath.c_str(), m_width, m_height);
            return Result::error(ErrorCode::ERR_EXPORTER_NOT_CONFIGURED, "Exporter not properly configured");
        }

        m_state = State::STARTING;
        m_progress = 0.0f;

        if (m_exportThread.joinable()) {
            m_exportThread.join();
        }

        m_exportThread = std::thread([this, timeline, compositor, onProgress, onComplete]() {
            auto start_time = std::chrono::high_resolution_clock::now();
            LOGI("Export thread started (iOS)");
            Result res = runExport(timeline, compositor, onProgress);
            auto end_time = std::chrono::high_resolution_clock::now();
            float total_duration_s = std::chrono::duration<float>(end_time - start_time).count();

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                if (res.isOk()) {
                    m_state = State::COMPLETED;
                    LOGI("Export completed successfully in %.2f seconds", total_duration_s);
                } else if (res.getErrorCode() == ErrorCode::ERR_EXPORTER_CANCELLED) {
                    m_state = State::CANCELED;
                    LOGI("Export canceled after %.2f seconds", total_duration_s);
                } else {
                    m_state = State::FAILED;
                    LOGE("Export failed after %.2f seconds: %s", total_duration_s, res.getMessage().c_str());
                }
            }

            if (onComplete) {
                onComplete(res);
            }
        });

        return Result::ok();
    }

    void cancel() override {
        m_state = State::CANCELED;
    }

    State getState() const override {
        return m_state;
    }

    float getProgress() const override {
        return m_progress;
    }

private:
    Result runExport(std::shared_ptr<Timeline> timeline,
                     std::shared_ptr<Compositor> compositor,
                     ProgressCallback onProgress) {

        if (!timeline || !compositor) return Result::error(ErrorCode::ERR_TIMELINE_NULL, "Invalid timeline or compositor");

        m_state = State::EXPORTING;

        @autoreleasepool {
            std::string currentChunkPath = m_outputPath;
            int chunkIndex = 0;
            if (m_chunkDurationNs > 0) {
                currentChunkPath = m_outputPath + "_chunk_" + std::to_string(chunkIndex) + ".mp4";
            }

            NSString* path = [NSString stringWithUTF8String:currentChunkPath.c_str()];
            NSURL* url = [NSURL fileURLWithPath:path];

            if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
                [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
            }

            NSError* error = nil;
            AVAssetWriter* assetWriter = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&error];
            if (error || !assetWriter) {
                return Result::error(ErrorCode::ERR_EXPORTER_MUXER_INIT_FAILED, "Failed to create AVAssetWriter");
            }

            NSDictionary* videoSettings = @{
                AVVideoCodecKey: AVVideoCodecTypeH264,
                AVVideoWidthKey: @(m_width),
                AVVideoHeightKey: @(m_height),
                AVVideoCompressionPropertiesKey: @{
                    AVVideoAverageBitRateKey: @(m_bitrate > 0 ? m_bitrate : 4000000),
                    AVVideoMaxKeyFrameIntervalKey: @(m_fps)
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
                return Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Cannot add video input");
            }

            EAGLContext* context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
            if (!context) {
                context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
            }
            if (!context) return Result::error(ErrorCode::ERR_EXPORTER_GL_CONTEXT_FAILED, "Failed to create background EAGLContext");

            EAGLContext* previousContext = [EAGLContext currentContext];
            [EAGLContext setCurrentContext:context];

            CVOpenGLESTextureCacheRef textureCache = NULL;
            CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, context, NULL, &textureCache);
            if (err != kCVReturnSuccess) {
                [EAGLContext setCurrentContext:previousContext];
                return Result::error(ErrorCode::ERR_EXPORTER_GL_CONTEXT_FAILED, "Failed to create texture cache");
            }

            [assetWriter startWriting];
            [assetWriter startSessionAtSourceTime:kCMTimeZero];

            int64_t totalDurationNs = timeline->getTotalDuration();
            int64_t frameDurationNs = 1000000000 / m_fps;
            int64_t currentTimeNs = 0;
            int64_t lastChunkStartNs = 0;

            GLuint fbo;
            glGenFramebuffers(1, &fbo);
            FrameBufferPtr cvExternalFbWrapper = std::make_shared<FrameBuffer>(m_width, m_height, fbo);

            Result exportResult = Result::ok();

            while (currentTimeNs <= totalDurationNs) {
                if (m_state == State::CANCELED) {
                    exportResult = Result::error(ErrorCode::ERR_EXPORTER_CANCELLED, "Export canceled by user");
                    break;
                }

                @autoreleasepool {
                    while (!videoInput.readyForMoreMediaData && m_state != State::CANCELED) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }

                    if (m_state == State::CANCELED) break;

                    CVPixelBufferRef pixelBuffer = NULL;
                    err = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, adaptor.pixelBufferPool, &pixelBuffer);
                    if (err != kCVReturnSuccess || !pixelBuffer) {
                        exportResult = Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Failed to create pixel buffer from pool");
                        break;
                    }

                    CVOpenGLESTextureRef cvTexture = NULL;
                    err = CVOpenGLESTextureCacheCreateTextureFromImage(
                        kCFAllocatorDefault, textureCache, pixelBuffer, NULL,
                        GL_TEXTURE_2D, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height,
                        GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0, &cvTexture);

                    if (err == kCVReturnSuccess && cvTexture) {
                        GLuint textureId = CVOpenGLESTextureGetName(cvTexture);

                        GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, fbo);
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
                        glViewport(0, 0, m_width, m_height);

                        cvExternalFbWrapper->setExternalFboId(fbo);

                        Result renderRes = compositor->renderFrameAtTime(currentTimeNs, cvExternalFbWrapper);
                        if (!renderRes.isOk()) {
                            LOGE("Export render failed at %lld: %s", (long long)currentTimeNs, renderRes.getMessage().c_str());
                            exportResult = renderRes;
                            CFRelease(cvTexture);
                            CVPixelBufferRelease(pixelBuffer);
                            break;
                        }

                        GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, 0);
                        glFinish();

                        CMTime presentationTime = CMTimeMake(currentTimeNs, 1000000000);
                        if (![adaptor appendPixelBuffer:pixelBuffer withPresentationTime:presentationTime]) {
                            exportResult = Result::error(ErrorCode::ERR_EXPORTER_ENCODER_INIT_FAILED, "Failed to append pixel buffer");
                            CFRelease(cvTexture);
                            CVPixelBufferRelease(pixelBuffer);
                            break;
                        }

                        CFRelease(cvTexture);
                        CVOpenGLESTextureCacheFlush(textureCache, 0);

                        // Chunking logic
                        if (m_chunkDurationNs > 0 && (currentTimeNs - lastChunkStartNs >= m_chunkDurationNs)) {
                            [videoInput markAsFinished];
                            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
                            [assetWriter finishWritingWithCompletionHandler:^{
                                dispatch_semaphore_signal(semaphore);
                            }];
                            dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

                            if (m_onChunkReady) {
                                m_onChunkReady(currentChunkPath, chunkIndex);
                            }

                            chunkIndex++;
                            currentChunkPath = m_outputPath + "_chunk_" + std::to_string(chunkIndex) + ".mp4";
                            NSString* nextPath = [NSString stringWithUTF8String:currentChunkPath.c_str()];
                            NSURL* nextUrl = [NSURL fileURLWithPath:nextPath];
                            if ([[NSFileManager defaultManager] fileExistsAtPath:nextPath]) {
                                [[NSFileManager defaultManager] removeItemAtPath:nextPath error:nil];
                            }

                            assetWriter = [[AVAssetWriter alloc] initWithURL:nextUrl fileType:AVFileTypeMPEG4 error:&error];
                            videoInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
                            videoInput.expectsMediaDataInRealTime = NO;
                            adaptor = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoInput
                                                                                 sourcePixelBufferAttributes:pixelBufferAttributes];
                            [assetWriter addInput:videoInput];
                            [assetWriter startWriting];
                            [assetWriter startSessionAtSourceTime:presentationTime];
                            lastChunkStartNs = currentTimeNs;
                        }
                    } else {
                        exportResult = Result::error(ErrorCode::ERR_EXPORTER_GL_CONTEXT_FAILED, "Failed to create texture from image");
                        CVPixelBufferRelease(pixelBuffer);
                        break;
                    }

                    CVPixelBufferRelease(pixelBuffer);

                    m_progress = static_cast<float>(currentTimeNs) / static_cast<float>(totalDurationNs);
                    if (onProgress) {
                        onProgress(m_progress);
                    }

                    currentTimeNs += frameDurationNs;
                }
            }

            if (exportResult.isOk()) {
                [videoInput markAsFinished];
                dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
                [assetWriter finishWritingWithCompletionHandler:^{
                    dispatch_semaphore_signal(semaphore);
                }];
                dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

                if (m_chunkDurationNs > 0 && m_onChunkReady) {
                    m_onChunkReady(currentChunkPath, chunkIndex);
                }
                m_progress = 1.0f;
            }

            glDeleteFramebuffers(1, &fbo);
            if (textureCache) CFRelease(textureCache);
            [EAGLContext setCurrentContext:previousContext];

            return exportResult;
        }
    }

    std::string m_outputPath;
    int m_width;
    int m_height;
    int m_fps;
    int m_bitrate;

    int64_t m_chunkDurationNs;
    ChunkCallback m_onChunkReady;

    std::atomic<State> m_state;
    std::mutex m_stateMutex;
    std::atomic<float> m_progress;
    std::thread m_exportThread;
};

// Only define create() on iOS
#ifndef __ANDROID__
std::unique_ptr<TimelineExporter> TimelineExporter::create() {
    return std::make_unique<TimelineExporterIOS>();
}
#endif

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // __APPLE__
