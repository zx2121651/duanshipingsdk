#include "../../include/timeline/DecoderPool.h"
#include <iostream>

#ifdef __ANDROID__
    // The implementations will be compiled separately, but we need to instantiate them.
    // For simplicity without forward declaring the concrete class, we can define a factory
    // or just include it if it's header-only/same module.
#endif

namespace sdk {
namespace video {
namespace timeline {

// Forward declarations of concrete decoders would go here,
// or ideally they are returned from a factory.
// We will mock the instantiation for architecture validation.

// Factory function implemented in VideoDecoderAndroid.cpp or VideoDecoderIOS.mm
extern std::shared_ptr<VideoDecoder> createPlatformDecoder();


DecoderPool::DecoderPool() {}

DecoderPool::~DecoderPool() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_decoders.clear();
}

void DecoderPool::registerMedia(const std::string& clipId, const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto ctx = std::make_shared<DecoderContext>();
    ctx->sourcePath = sourcePath;
    ctx->decoder = createPlatformDecoder();

    if (ctx->decoder) {
        ctx->decoder->open(sourcePath);
    }

    m_decoders[clipId] = ctx;
    std::cout << "[DecoderPool] Registered media " << clipId << " from " << sourcePath << std::endl;
}

void DecoderPool::releaseMedia(const std::string& clipId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_decoders.erase(clipId);
    std::cout << "[DecoderPool] Released media " << clipId << std::endl;
}

Texture DecoderPool::getFrame(const std::string& clipId, int64_t localTimeUs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_decoders.find(clipId);
    if (it == m_decoders.end()) {
        std::cerr << "[DecoderPool] Decoder context not found for clip " << clipId << std::endl;
        return {0, 0, 0};
    }

    auto ctx = it->second;

    if (ctx->decoder) {
        // Platform hardware decoding
        Texture tex = ctx->decoder->getFrameAt(localTimeUs);
        if (tex.id != 0) {
            ctx->lastDecodedFrame = tex;
            ctx->lastDecodedTimeUs = localTimeUs;
        }
        return ctx->lastDecodedFrame;
    } else {
        // Fallback dummy logic
        if (!ctx->isInitialized) {
            ctx->lastDecodedFrame = {1, 1920, 1080}; // Dummy ID
            ctx->isInitialized = true;
        }
        ctx->lastDecodedTimeUs = localTimeUs;
        return ctx->lastDecodedFrame;
    }
}

} // namespace timeline
} // namespace video
} // namespace sdk
