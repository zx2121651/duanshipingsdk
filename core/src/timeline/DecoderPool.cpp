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

ResultPayload<Texture> DecoderPool::getFrame(const std::string& clipId, int64_t localTimeNs, bool requiresExactSeek) {
    std::unique_lock<std::mutex> lock(m_mutex);

    auto it = m_decoders.find(clipId);
    if (it == m_decoders.end()) {
        std::cerr << "[DecoderPool] Decoder context not found for clip " << clipId << std::endl;
        return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND, "Decoder context not found for clip " + clipId);
    }

    auto ctx = it->second;
    ctx->lastAccessCounter = ++m_accessCounter;

    // 如果要求精确 Seek 或者硬件解码已经失败过了，走软解降级路线
    bool useSoftwareDecoder = requiresExactSeek || ctx->hwFailed;

    if (useSoftwareDecoder) {
        if (!ctx->softDecoder) {
            ctx->softDecoder = createSoftwareDecoder();
            auto openRes = ctx->softDecoder->open(ctx->sourcePath);
            if (!openRes.isOk()) {
                std::cerr << "[DecoderPool] Failed to open Software Decoder for clip " << clipId << ": " << openRes.getMessage() << std::endl;
                return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED, "Failed to open software decoder: " + openRes.getMessage());
            }
            std::cout << "[DecoderPool] Falling back to Software Decoder for clip " << clipId << std::endl;
        }
        Result seekRes = ctx->softDecoder->seekExact(localTimeNs);
        if (seekRes.isOk()) {
            auto frameRes = ctx->softDecoder->getFrameAt(localTimeNs);
            if (frameRes.isOk()) {
                Texture tex = frameRes.getValue();
                ctx->lastDecodedFrame = tex;
                ctx->lastDecodedTimeNs = localTimeNs;
                ctx->isInitialized = true;
                return frameRes;
            }
            std::cerr << "[DecoderPool] Software decoder returned error for clip " << clipId << ": " << frameRes.getMessage() << std::endl;
            return frameRes;
        } else {
            std::cerr << "[DecoderPool] Software seek failed for clip " << clipId << ": " << seekRes.getMessage() << std::endl;
            return ResultPayload<Texture>::error(ErrorCode::ERR_DECODER_SEEK_FAILED, "Software seek failed: " + seekRes.getMessage());
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
                std::cerr << "[DecoderPool] Failed to activate decoder for clip " << clipId << ": " << res.getMessage() << std::endl;
                ctx->decoder = nullptr;
                ctx->hwFailed = true; // 硬件解码器直接报废，未来全走软解
            }
        } else {
            std::cerr << "[DecoderPool] createPlatformDecoder returned null for clip " << clipId << std::endl;
            ctx->hwFailed = true;
        }
    }

    if (ctx->decoder && !ctx->hwFailed) {
        // 先尝试精准 Seek，检查硬件解码器是否支持该跳变
        Result seekRes = ctx->decoder->seekExact(localTimeNs);
        if (!seekRes.isOk()) {
            std::cerr << "[DecoderPool] Hardware seek failed for clip " << clipId << ": " << seekRes.getMessage() << ". Degrading to software." << std::endl;
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
        auto frameRes = ctx->decoder->getFrameAt(localTimeNs);
        if (frameRes.isOk()) {
            Texture tex = frameRes.getValue();
            ctx->lastDecodedFrame = tex;
            ctx->lastDecodedTimeNs = localTimeNs;
            ctx->isInitialized = true;
            return frameRes;
        } else {
            // 如果硬件解码器返回 Fatal 错误（比如设备丢失、解码器崩溃），触发降级
            if (frameRes.getErrorCode() == ErrorCode::ERR_DECODER_HW_FAILURE) {
                std::cerr << "[DecoderPool] Hardware decoder fatal failure for clip " << clipId << ": " << frameRes.getMessage() << ". Degrading to software." << std::endl;
                ctx->hwFailed = true;
                ctx->decoder->close();
                ctx->decoder = nullptr;
                m_activeDecoderCount--;

                lock.unlock();
                return getFrame(clipId, localTimeNs, true);
            }

            // 否则可能是暂时性的丢帧 (ERR_DECODER_FRAME_DROP)
            std::cerr << "[DecoderPool] Hardware decoder returned error for clip " << clipId << ": " << frameRes.getMessage() << std::endl;
            return frameRes;
        }
    } else {
        // Both HW and SW (if tried) failed, or we are in a state where we can't decode.
        std::cerr << "[DecoderPool] Both HW and SW decoding failed or unavailable for clip " << clipId << std::endl;
        return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED, "Decoding failed or unavailable");
    }
}

} // namespace timeline
} // namespace video
} // namespace sdk
