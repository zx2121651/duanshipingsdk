#include "../../include/timeline/DecoderPool.h"
#include <iostream>

namespace sdk {
namespace video {
namespace timeline {

DecoderPool::DecoderPool() {}

DecoderPool::~DecoderPool() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_decoders.clear();
}

void DecoderPool::registerMedia(const std::string& clipId, const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto ctx = std::make_shared<DecoderContext>();
    ctx->sourcePath = sourcePath;
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

    // --- 真实实现预留区 ---
    // 这里未来将使用 NDK 的 AMediaExtractor_seekTo 配合 AMediaCodec 队列，
    // 将对应时间点的 H.264 视频流解压，并绑定到 SurfaceTexture / OES 纹理上，最后转为 RGB。

    if (!ctx->isInitialized) {
        // [占位逻辑] 在未接入真实硬件解码器前，我们为了打通整个 NLE 离屏合成和滤镜渲染链路，
        // 将伪造一个全红色的“测试占位纹理”，用来证明 Compositor 能从这里拿走数据并混合。

        ctx->lastDecodedFrame = {0, 1920, 1080}; // Dummy ID, usually an OES to RGB converted texture
        ctx->isInitialized = true;
    }

    // 防抖与帧复用逻辑。如果当前时间戳并没有跳过这帧的物理时长，可以直接返回缓存的 lastDecodedFrame，
    // 而不需要向硬解码器发请求 (降低功耗)。
    ctx->lastDecodedTimeUs = localTimeUs;

    return ctx->lastDecodedFrame;
}

} // namespace timeline
} // namespace video
} // namespace sdk
