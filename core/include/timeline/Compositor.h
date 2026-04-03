#pragma once
#include "Timeline.h"
#include "../FilterEngine.h"
#include <memory>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 离屏合成器 (Offscreen Compositor)
 *
 * 真正将 NLE Timeline 数据模型转化为图像的执行单元。
 * 它根据当前时间 T，从 Timeline 索取所有可见的 Clip，向 DecoderPool 请求它们的解码帧，
 * 然后利用 FilterEngine 提供的混合能力 (Blending) 和 FBO 将它们拍成一张最终图像。
 */
class Compositor {
public:
    Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine);
    ~Compositor() = default;

    /**
     * @brief 渲染指定时刻的一帧
     * @param timelineUs 时间线上的微秒时刻
     * @param outputFb 输出的目标 FBO (如果是预览则是屏幕，导出则是编码器输入的 FBO)
     */
    void renderFrameAtTime(int64_t timelineUs, FrameBufferPtr outputFb);

private:
    std::shared_ptr<Timeline> m_timeline;
    std::shared_ptr<FilterEngine> m_filterEngine;

    // std::shared_ptr<DecoderPool> m_decoderPool; // TODO: 挂载解码池
};

} // namespace timeline
} // namespace video
} // namespace sdk
