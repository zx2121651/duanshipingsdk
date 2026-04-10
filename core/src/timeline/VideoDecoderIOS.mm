#include "../../include/timeline/VideoDecoder.h"

#ifdef __APPLE__
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

namespace sdk {
namespace video {
namespace timeline {

class VideoDecoderIOS : public VideoDecoder {
public:
    VideoDecoderIOS() : m_assetReader(nullptr), m_trackOutput(nullptr), m_textureCache(nullptr), m_cvTexture(nullptr), m_width(0), m_height(0), m_running(false) {}

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

        m_running = true;
        m_decodeThread = std::thread(&VideoDecoderIOS::decodeLoop, this);

        return Result::ok();
    }

    void decodeLoop() {
        while (m_running && [m_assetReader status] == AVAssetReaderStatusReading) {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_queueCv.wait(lock, [this]() { return m_frameQueue.size() < 3 || !m_running; });
            }
            if (!m_running) break;

            CMSampleBufferRef sampleBuffer = [m_trackOutput copyNextSampleBuffer];
            if (sampleBuffer) {
                CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
                if (pixelBuffer) {
                    CVPixelBufferRetain(pixelBuffer); // Retain to pass across thread

                    std::shared_ptr<FrameBufferPacket> packet = std::make_shared<FrameBufferPacket>();
                    packet->ptsUs = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sampleBuffer)) * 1000000;
                    packet->width = m_width;
                    packet->height = m_height;
                    packet->nativeBuffer = pixelBuffer;

                    {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_frameQueue.push(packet);
                    }
                }
                CFRelease(sampleBuffer);
            } else {
                break; // EOF
            }
        }
    }

    Texture getFrameAt(int64_t timeUs) override {
        std::shared_ptr<FrameBufferPacket> targetPacket = nullptr;

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            while (!m_frameQueue.empty() && m_frameQueue.front()->ptsUs < timeUs - 30000) {
                CVPixelBufferRelease((CVPixelBufferRef)m_frameQueue.front()->nativeBuffer);
                m_frameQueue.pop();
                m_queueCv.notify_one();
            }

            if (!m_frameQueue.empty()) {
                targetPacket = m_frameQueue.front();
            }
        }

        if (targetPacket && m_textureCache) {
            CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)targetPacket->nativeBuffer;

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
                return {textureId, (uint32_t)m_width, (uint32_t)m_height};
            }
        }

        return {0, 0, 0};
    }

    void close() override {
        m_running = false;
        m_queueCv.notify_all();
        if (m_decodeThread.joinable()) {
            m_decodeThread.join();
        }

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

        while (!m_frameQueue.empty()) {
            CVPixelBufferRelease((CVPixelBufferRef)m_frameQueue.front()->nativeBuffer);
            m_frameQueue.pop();
        }
    }

private:
    AVAssetReader* m_assetReader;
    AVAssetReaderTrackOutput* m_trackOutput;
    CVOpenGLESTextureCacheRef m_textureCache;
    CVOpenGLESTextureRef m_cvTexture;
    int32_t m_width;
    int32_t m_height;

    std::atomic<bool> m_running;
    std::thread m_decodeThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::queue<std::shared_ptr<FrameBufferPacket>> m_frameQueue;
};

} // namespace timeline
} // namespace video
} // namespace sdk

#endif // __APPLE__

// Platform Decoder Factory Implementation
std::shared_ptr<VideoDecoder> createPlatformDecoder() {
    return std::make_shared<VideoDecoderIOS>();
}
