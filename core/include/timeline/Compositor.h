#pragma once
#include "Timeline.h"
#include "../FilterEngine.h"
#include "../GLTypes.h"
#include <memory>
#include <vector>
#include <functional>

namespace sdk {
namespace video {
namespace timeline {

/**
 * 解码器接口 (Decoder Interface)
 * 用于根据 Clip ID 和相对时间戳，提取出对应的帧纹理
 */
class IDecoderPool {
public:
    virtual ~IDecoderPool() = default;

    // 返回 {textureId, width, height}，若解码失败返回 {0,0,0}
    virtual Texture getFrame(const std::string& clipId, int64_t localTimeUs) = 0;
};

/**
 * @brief 离屏合成器 (Offscreen Compositor)
 *
 * 根据当前时间 T，从 Timeline 索取所有可见的 Clip，向 DecoderPool 请求解码帧，
 * 最后利用 FilterEngine 提供的混合能力将它们拍成一张最终图像。
 */
class Compositor {
public:
    Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine);
    ~Compositor() = default;

    void setDecoderPool(std::shared_ptr<IDecoderPool> decoderPool) {
        m_decoderPool = decoderPool;
    }

    /**
     * @brief 渲染指定时刻的一帧
     * @param timelineUs 时间线上的微秒时刻
     * @param outputFb 输出的目标 FBO
     * @return 成功返回 true，发生错误返回 false
     */
    Result renderFrameAtTime(int64_t timelineUs, FrameBufferPtr outputFb);

private:
    std::shared_ptr<Timeline> m_timeline;
    std::shared_ptr<FilterEngine> m_filterEngine;
    std::shared_ptr<IDecoderPool> m_decoderPool;

    GLuint m_blendProgram = 0;
    GLuint m_copyProgram = 0;
    void initCopyProgram();
    void copyTexture(const Texture& src, FrameBufferPtr target);

    void initBlendProgram();
    Texture blendTextures(const Texture& bg, const Texture& fg, float opacity, FrameBufferPtr target);
};

} // namespace timeline
} // namespace video
} // namespace sdk
