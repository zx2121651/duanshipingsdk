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
    int64_t ptsUs;
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
    virtual Texture getFrameAt(int64_t timeUs) = 0;

    virtual void close() = 0;
};

} // namespace timeline
} // namespace video
} // namespace sdk
