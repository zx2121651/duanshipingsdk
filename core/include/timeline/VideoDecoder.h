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
    // 如果缓存中有，则取出并转换/上传到 Texture 返回；如果没准备好，则返回 0
    virtual Texture getFrameAt(int64_t timeNs) = 0;

    // 精准 Seek，用于倒放或复杂转场时的 B 帧规避
    // 如果硬件解码器不支持或者跨度过大导致花屏，允许返回 Error，由上层退化到软解
    virtual Result seekExact(int64_t timeNs) { return Result::ok(); }

    virtual void close() = 0;
};

/**
 * @brief 自研软件解码器 (占位)
 * 用于在硬解失败或精准抽帧时提供基于 FFMpeg 的 CPU 软解支持，彻底解决 B 帧寻址漂移噩梦。
 */
class SoftwareVideoDecoder : public VideoDecoder {
public:
    Result open(const std::string& filePath) override { return Result::ok(); }
    Texture getFrameAt(int64_t timeNs) override { return {0, 0, 0}; }
    Result seekExact(int64_t timeNs) override { return Result::ok(); }
    void close() override {}
};


} // namespace timeline
} // namespace video
} // namespace sdk
