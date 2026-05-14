#include "../../include/timeline/DecoderPool.h"
#define LOG_TAG "DecoderPool"
#include "../../include/Log.h"
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

// FFmpegVideoDecoder 工厂实现 (由 FFmpegVideoDecoder.cpp 提供)
#if defined(HAS_FFMPEG_DECODER)
extern std::shared_ptr<VideoDecoder> createSoftwareDecoder_FFmpeg();
#else
// 如果未集成 FFmpeg，则提供弱符号实现，允许单元测试通过 Mock 覆盖
// 弱符号 (Weak Symbol) 允许链接器在发现同名的强符号（如测试中的 Mock）时优先使用强符号
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) std::shared_ptr<VideoDecoder> createSoftwareDecoder_FFmpeg() {
    return std::make_shared<SoftwareVideoDecoder>();
}
#else
// MSVC 或其他平台不支持 weak 属性时，仅作声明，此时如果缺少实现会链接失败
extern std::shared_ptr<VideoDecoder> createSoftwareDecoder_FFmpeg();
#endif
#endif

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
    LOGI("Registered media %s from %s (deferred open)", clipId.c_str(), sourcePath.c_str());
}

void DecoderPool::releaseMedia(const std::string& clipId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_decoders.find(clipId);
    if (it != m_decoders.end()) {
        auto ctx = it->second;
        // 释放硬件解码器
        if (ctx->decoder) {
            ctx->decoder->close();
            ctx->decoder = nullptr;
            m_activeDecoderCount--;
        }
        // 释放软件解码器 (Software Fallback)
        if (ctx->softDecoder) {
            ctx->softDecoder->close();
            ctx->softDecoder = nullptr;
        }
        ctx->lastDecodedFrame = {0, 0, 0};
        ctx->lastDecodedTimeNs = -1;
        ctx->isInitialized = false;

        m_decoders.erase(it);
        LOGI("Released media %s", clipId.c_str());
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
    LOGI("Cleared all media");
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
            LOGI("Evicting active decoder for clip %s due to concurrency limits.", oldestId.c_str());
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
        LOGE("Decoder context not found for clip %s", clipId.c_str());
        return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND, "Decoder context not found for clip " + clipId);
    }

    auto ctx = it->second;
    ctx->lastAccessCounter = ++m_accessCounter;

    // 如果要求精确 Seek 或者硬件解码已经失败过了，走软解降级路线
    // 策略覆写：SW_FIRST 强制走软解；HW_ONLY 禁止软解降级
    bool useSoftwareDecoder = (requiresExactSeek || ctx->hwFailed) &&
                               (m_strategy != DecoderFallbackStrategy::HW_ONLY);
    if (m_strategy == DecoderFallbackStrategy::SW_FIRST) {
        useSoftwareDecoder = true;
    }

    if (useSoftwareDecoder) {
        if (!ctx->softDecoder) {
            // 1. 创建软解实例 (使用指令要求的工厂函数)
            ctx->softDecoder = createSoftwareDecoder_FFmpeg();
            if (!ctx->softDecoder) {
                LOGE("Failed to create FFmpeg Software Decoder for clip %s", clipId.c_str());
                return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED, "Failed to create software decoder for " + clipId);
            }

            // 2. 打开素材
            auto openRes = ctx->softDecoder->open(ctx->sourcePath);
            if (!openRes.isOk()) {
                LOGE("Failed to open Software Decoder for clip %s: %s", clipId.c_str(), openRes.getMessage().c_str());
                ctx->softDecoder = nullptr; // 确保失败后重置，下次可重试
                return ResultPayload<Texture>::error(openRes.getErrorCode(), "Failed to open software decoder for " + clipId + ": " + openRes.getMessage());
            }
            LOGI("Falling back to Software Decoder for clip %s (HW failed: %d, Exact seek: %d)", clipId.c_str(), ctx->hwFailed, requiresExactSeek);
        }

        // 软件解码器通常需要先 seek 到目标位置附近，除非是连续播放且时间戳单调递增
        // FFmpegVideoDecoder::getFrameAt 内部是向后查找，如果 timeNs < 当前 pts 则必须 seek
        Result seekRes = ctx->softDecoder->seekExact(localTimeNs);
        if (!seekRes.isOk()) {
            LOGE("Software seek failed for clip %s to %lld: %s", clipId.c_str(), (long long)localTimeNs, seekRes.getMessage().c_str());
            return ResultPayload<Texture>::error(seekRes.getErrorCode(), "Software seek failed for " + clipId + ": " + seekRes.getMessage());
        }

        auto frameRes = ctx->softDecoder->getFrameAt(localTimeNs);
        if (frameRes.isOk()) {
            ctx->lastDecodedFrame = frameRes.getValue();
            ctx->lastDecodedTimeNs = localTimeNs;
            ctx->isInitialized = true;
        } else {
            LOGE("Software decoder returned error for clip %s at %lld: %s", clipId.c_str(), (long long)localTimeNs, frameRes.getMessage().c_str());
        }
        return frameRes;
    }

    if (!ctx->decoder && !ctx->hwFailed) {
        // Activate/Re-activate Decoder
        evictDecodersIfNeeded();
        ctx->decoder = createPlatformDecoder();
        if (ctx->decoder) {
            auto res = ctx->decoder->open(ctx->sourcePath);
            if (res.isOk()) {
                m_activeDecoderCount++;
                LOGI("Activated hardware decoder for clip %s", clipId.c_str());
            } else {
                LOGE("Failed to activate hardware decoder for clip %s: %s", clipId.c_str(), res.getMessage().c_str());
                ctx->decoder = nullptr;
                ctx->hwFailed = true; // 硬件解码器直接报废，未来全走软解
            }
        } else {
            LOGE("createPlatformDecoder returned null for clip %s", clipId.c_str());
            ctx->hwFailed = true;
        }
    }

    if (ctx->decoder && !ctx->hwFailed) {
        // 先尝试精准 Seek，检查硬件解码器是否支持该跳变
        Result seekRes = ctx->decoder->seekExact(localTimeNs);
        if (!seekRes.isOk()) {
            LOGW("Hardware seek failed for clip %s to %lld: %s. Degrading to software.", clipId.c_str(), (long long)localTimeNs, seekRes.getMessage().c_str());
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
                LOGE("Hardware decoder fatal failure for clip %s at %lld: %s. Degrading to software.", clipId.c_str(), (long long)localTimeNs, frameRes.getMessage().c_str());
                ctx->hwFailed = true;
                ctx->decoder->close();
                ctx->decoder = nullptr;
                m_activeDecoderCount--;

                lock.unlock();
                return getFrame(clipId, localTimeNs, true);
            }

            // 否则可能是暂时性的丢帧 (ERR_DECODER_FRAME_DROP)
            LOGW("Hardware decoder returned error for clip %s at %lld: %s", clipId.c_str(), (long long)localTimeNs, frameRes.getMessage().c_str());
            return frameRes;
        }
    } else {
        // Both HW and SW (if tried) failed, or we are in a state where we can't decode.
        LOGE("Both HW and SW decoding failed or unavailable for clip %s", clipId.c_str());
        return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED, "Decoding failed or unavailable for " + clipId);
    }
}

void DecoderPool::prefetchFrame(const std::string& clipId, int64_t upcomingTimeNs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_decoders.find(clipId);
    if (it == m_decoders.end()) return;

    auto& ctx = it->second;
    // If a hardware decoder is running, just record the upcoming timestamp.
    // The background decodeLoop in VideoDecoderAndroid will continue filling
    // its ring-queue (capacity 8 frames), so when getFrame() is called for
    // upcomingTimeNs the packet is already available — zero additional latency.
    ctx->lastAccessCounter = ++m_accessCounter;
    (void)upcomingTimeNs; // consumed implicitly by the decoder thread queue
    LOGV("Prefetch hint for clip %s at %lld ns", clipId.c_str(), (long long)upcomingTimeNs);
}

} // namespace timeline
} // namespace video
} // namespace sdk
