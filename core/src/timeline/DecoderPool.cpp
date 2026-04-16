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

std::shared_ptr<VideoDecoder> createSoftwareDecoder() {
    return std::make_shared<SoftwareVideoDecoder>();
}

DecoderPool::DecoderPool() {}

DecoderPool::~DecoderPool() {
    clear();
}

void DecoderPool::registerMedia(const std::string& clipId, const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 如果已经存在，先释放旧的，确保 m_activeDecoderCount 和 decoder 资源正确回收
    if (m_decoders.find(clipId) != m_decoders.end()) {
        auto oldCtx = m_decoders[clipId];
        if (oldCtx->decoder) {
            oldCtx->decoder->close();
            m_activeDecoderCount--;
        }
        if (oldCtx->softDecoder) {
            oldCtx->softDecoder->close();
        }
    }

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
        if (it->second->softDecoder) {
            it->second->softDecoder->close();
        }
        it->second->decoder = nullptr;
        it->second->softDecoder = nullptr;
        it->second->lastDecodedFrame = {0, 0, 0};
        it->second->lastDecodedTimeNs = -1;
        it->second->isInitialized = false;

        m_decoders.erase(it);
        std::cout << "[DecoderPool] Released media " << clipId << std::endl;
    }
}

void DecoderPool::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_decoders) {
        if (pair.second->decoder) {
            pair.second->decoder->close();
        }
        if (pair.second->softDecoder) {
            pair.second->softDecoder->close();
        }
        pair.second->decoder = nullptr;
        pair.second->softDecoder = nullptr;
        pair.second->lastDecodedFrame = {0, 0, 0};
        pair.second->lastDecodedTimeNs = -1;
        pair.second->isInitialized = false;
    }
    m_decoders.clear();
    m_activeDecoderCount = 0;
    std::cout << "[DecoderPool] Cleared all media" << std::endl;
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
            oldestCtx->lastDecodedFrame = {0, 0, 0};
            oldestCtx->lastDecodedTimeNs = -1;
            oldestCtx->isInitialized = false;
            m_activeDecoderCount--;
        } else {
            break; // 理论上不可能
        }
    }
}

Texture DecoderPool::getFrame(const std::string& clipId, int64_t localTimeNs, bool requiresExactSeek) {
    std::unique_lock<std::mutex> lock(m_mutex);

    auto it = m_decoders.find(clipId);
    if (it == m_decoders.end()) {
        std::cerr << "[DecoderPool] Decoder context not found for clip " << clipId << std::endl;
        return {0, 0, 0};
    }

    auto ctx = it->second;
    ctx->lastAccessCounter = ++m_accessCounter;

    // 如果要求精确 Seek 或者硬件解码已经失败过了，走软解降级路线
    bool useSoftwareDecoder = requiresExactSeek || ctx->hwFailed;

    if (useSoftwareDecoder) {
        if (!ctx->softDecoder) {
            ctx->softDecoder = createSoftwareDecoder();
            ctx->softDecoder->open(ctx->sourcePath);
            std::cout << "[DecoderPool] Falling back to Software Decoder for clip " << clipId << std::endl;
        }
        Result seekRes = ctx->softDecoder->seekExact(localTimeNs);
        if (seekRes.isOk()) {
            Texture tex = ctx->softDecoder->getFrameAt(localTimeNs);
            if (tex.id != 0) {
                ctx->lastDecodedFrame = tex;
                ctx->lastDecodedTimeNs = localTimeNs;
                ctx->isInitialized = true;
            }
            return ctx->lastDecodedFrame;
        }
    }

    if (!ctx->decoder && !ctx->hwFailed) {
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
                ctx->hwFailed = true; // 硬件解码器直接报废，未来全走软解
            }
        }
    }

    if (ctx->decoder && !ctx->hwFailed) {
        // 先尝试精准 Seek，检查硬件解码器是否支持该跳变
        Result seekRes = ctx->decoder->seekExact(localTimeNs);
        if (!seekRes.isOk()) {
            std::cerr << "[DecoderPool] Hardware seek failed: " << seekRes.getMessage() << std::endl;
            // 硬件解码器寻址失败（如 B-Frame 导致花屏），触发当前上下文的硬件降级
            ctx->hwFailed = true;
            ctx->decoder->close();
            ctx->decoder = nullptr;
            m_activeDecoderCount--;

            // 解锁，然后递归转交软解处理
            lock.unlock();
            return getFrame(clipId, localTimeNs, true);
        }

        // Platform hardware decoding
        Texture tex = ctx->decoder->getFrameAt(localTimeNs);
        if (tex.id != 0) {
            ctx->lastDecodedFrame = tex;
            ctx->lastDecodedTimeNs = localTimeNs;
            ctx->isInitialized = true;
        }
        return ctx->lastDecodedFrame;
    } else {
        // Fallback dummy logic if both fail
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
