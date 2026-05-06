#pragma once
#include "../GLTypes.h"
#include <string>
#include <memory>
#include <vector>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 异步解码器的内存缓冲包
 */
struct FrameBufferPacket {
    int64_t ptsNs;
    int32_t width;
    int32_t height;
    std::vector<uint8_t> data; // 存 YUV (Android)
    void* nativeBuffer = nullptr; // 存 CVPixelBufferRef (iOS)
};

/**
 * @brief 统一硬件视频解码器接口
 */
class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;

    virtual Result open(const std::string& filePath) = 0;

    // 主渲染线程调用：获取时刻的纹理。
    // 如果缓存中有，则取出并转换/上传到 Texture 返回。
    virtual ResultPayload<Texture> getFrameAt(int64_t timeNs) = 0;

    // 精准 Seek，用于倒放或复杂转场时的 B 帧规避
    // 如果硬件解码器不支持或者跨度过大导致花屏，允许返回 Error，由上层退化到软解
    virtual Result seekExact(int64_t timeNs) { return Result::ok(); }

    virtual void close() = 0;
};

/**
 * @brief 自研软件解码器 (FFmpeg 集成占位，暂不可用)
 *
 * 当硬件解码器报告 hwFailed=true 后，DecoderPool 会尝试此路径。
 * 目前 FFmpeg 尚未集成，所有方法均返回明确错误码，
 * 防止上层把零纹理 {0,0,0} 误当有效帧渲染出黑帧。
 *
 * TODO: 集成 FFmpeg libavcodec 后替换 open() / getFrameAt() 实现。
 */
class SoftwareVideoDecoder : public VideoDecoder {
public:
    Result open(const std::string& filePath) override {
        // FFmpeg 尚未集成，明确拒绝，避免 DecoderPool 误以为软解可用
        return Result::error(ErrorCode::ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED,
            "SoftwareVideoDecoder: FFmpeg not integrated, file=" + filePath);
    }

    ResultPayload<Texture> getFrameAt(int64_t timeNs) override {
        return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED,
            "SoftwareVideoDecoder: FFmpeg not integrated");
    }

    Result seekExact(int64_t timeNs) override {
        return Result::error(ErrorCode::ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED,
            "SoftwareVideoDecoder: FFmpeg not integrated");
    }

    void close() override {}

    bool isOpen() const { return false; }
};


} // namespace timeline
} // namespace video
} // namespace sdk
