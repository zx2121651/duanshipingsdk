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
    ctx->decoder = nullptr; // 懒加载，防止瞬间启动过多 decoder
    ctx->lastAccessCounter = ++m_accessCounter;

    m_decoders[clipId] = ctx;
    std::cout << "[DecoderPool] Registered media " << clipId << " from " << sourcePath << " (deferred open)" << std::endl;
}

void DecoderPool::releaseMedia(const std::string& clipId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_decoders.find(clipId);
    if (it != m_decoders.end()) {
        if (it->second->decoder) {
            it->second->decoder->close();
            m_activeDecoderCount--;
        }
        m_decoders.erase(it);
        std::cout << "[DecoderPool] Released media " << clipId << std::endl;
    }
}

void DecoderPool::evictDecodersIfNeeded() {
    while (m_activeDecoderCount >= MAX_CONCURRENT_DECODERS) {
        std::shared_ptr<DecoderContext> oldestCtx = nullptr;
        uint64_t oldestTime = ~(0ULL);
        std::string oldestId;

        for (const auto& pair : m_decoders) {
            if (pair.second->decoder && pair.second->lastAccessCounter < oldestTime) {
                oldestTime = pair.second->lastAccessCounter;
                oldestCtx = pair.second;
                oldestId = pair.first;
            }
        }

        if (oldestCtx) {
            std::cout << "[DecoderPool] Evicting active decoder for clip " << oldestId << " due to concurrency limits." << std::endl;
            oldestCtx->decoder->close();
            oldestCtx->decoder = nullptr;
            m_activeDecoderCount--;
        } else {
            break; // 理论上不可能
        }
    }
}

Texture DecoderPool::getFrame(const std::string& clipId, int64_t localTimeNs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_decoders.find(clipId);
    if (it == m_decoders.end()) {
        std::cerr << "[DecoderPool] Decoder context not found for clip " << clipId << std::endl;
        return {0, 0, 0};
    }

    auto ctx = it->second;
    ctx->lastAccessCounter = ++m_accessCounter;

    if (!ctx->decoder) {
        // Activate/Re-activate Decoder
        evictDecodersIfNeeded();
        ctx->decoder = createPlatformDecoder();
        if (ctx->decoder) {
            auto res = ctx->decoder->open(ctx->sourcePath);
            if (res.isOk()) {
                m_activeDecoderCount++;
                std::cout << "[DecoderPool] Activated decoder for clip " << clipId << std::endl;
            } else {
                std::cerr << "[DecoderPool] Failed to activate decoder for clip " << clipId << std::endl;
                ctx->decoder = nullptr;
            }
        }
    }

    if (ctx->decoder) {
        // Platform hardware decoding
        Texture tex = ctx->decoder->getFrameAt(localTimeNs);
        if (tex.id != 0) {
            ctx->lastDecodedFrame = tex;
            ctx->lastDecodedTimeNs = localTimeNs;
            ctx->isInitialized = true;
        }
        return ctx->lastDecodedFrame;
    } else {
        // Fallback dummy logic
        if (!ctx->isInitialized) {
            ctx->lastDecodedFrame = {1, 1920, 1080}; // Dummy ID
            ctx->isInitialized = true;
        }
        ctx->lastDecodedTimeNs = localTimeNs;
        return ctx->lastDecodedFrame;
    }
}

} // namespace timeline
} // namespace video
} // namespace sdk
