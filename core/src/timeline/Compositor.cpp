#include "../../include/timeline/Compositor.h"

namespace sdk {
namespace video {
namespace timeline {

Compositor::Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine)
    : m_timeline(timeline), m_filterEngine(engine) {}

void Compositor::renderFrameAtTime(int64_t timelineUs, FrameBufferPtr outputFb) {
    if (!m_timeline || !m_filterEngine || !outputFb) return;

    // 1. 数据驱动：向 Timeline 索取当前时刻需要显示的视频/图片层
    std::vector<ClipPtr> activeClips = m_timeline->getActiveVideoClipsAtTime(timelineUs);

    if (activeClips.empty()) {
        // 如果当前帧没有任何可见的素材，直接清空输出 FBO 为黑屏 (黑场转场)
        outputFb->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        outputFb->unbind();
        return;
    }

    // 2. 混合树合成 (Compositing Pass)
    // 真实情况：我们需要一个混合 Filter (AlphaBlendFilter 等) 将多个纹理叠加。
    // 以下为概念验证代码：遍历所有层，如果底层有 DecoderPool，则拉取帧。

    /*
    FrameBufferPtr currentLayerFb = m_filterEngine->m_frameBufferPool.getFrameBuffer(outputFb->width(), outputFb->height());

    for (const auto& clip : activeClips) {
        // A. 计算由于剪辑和变速带来的局部相对时间
        // int64_t localTimeUs = (timelineUs - clip->getTimelineIn()) * clip->getSpeed() + clip->getTrimIn();

        // B. 请求 DecoderPool (底层通过 MediaExtractor/AVAssetReader 获取这一帧纹理)
        // Texture clipTex = m_decoderPool->getFrame(clip->getId(), localTimeUs);

        // C. 将这个 clipTex 与我们当前的 currentLayerFb 做混合 (Alpha Blend)，或者应用矩阵缩放 (PiP 画中画)
        // currentLayerFb = m_filterEngine->blend(currentLayerFb, clipTex, clip->getTransformMatrix(), clip->getOpacity());
    }

    // 3. 最终将合并好的帧写入指定的 outputFb
    // m_filterEngine->copyOrApplyFinalEffect(currentLayerFb, outputFb);

    m_filterEngine->m_frameBufferPool.returnFrameBuffer(currentLayerFb.get());
    */
}

} // namespace timeline
} // namespace video
} // namespace sdk
