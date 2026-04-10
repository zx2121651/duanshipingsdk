#pragma once

#include "PipelineNode.h"
#include "../timeline/Timeline.h"
#include "../timeline/Compositor.h"

namespace sdk {
namespace video {

class TimelineNode : public PipelineNode {
public:
    TimelineNode(const std::string& name, std::shared_ptr<timeline::Timeline> timeline, std::shared_ptr<timeline::Compositor> compositor)
        : PipelineNode(name), m_timeline(timeline), m_compositor(compositor) {}

    VideoFrame pullFrame(int64_t timestampNs) override {
        if (!m_timeline || !m_compositor) return VideoFrame();

        // Convert ns to ms or internal timeline units
        int64_t timeUs = timestampNs / 1000;

        int width = m_timeline->getOutputWidth();
        int height = m_timeline->getOutputHeight();

        FrameBufferPtr fbo = m_frameBufferPool.getFrameBuffer(width, height);
        Result res = m_compositor->renderFrameAtTime(timeUs, fbo);

        VideoFrame frame;
        if (fbo) {
            frame.textureId = fbo->getTexture().id;
            frame.width = fbo->getTexture().width;
            frame.height = fbo->getTexture().height;
            frame.frameBuffer = fbo;
        }
        frame.timestampNs = timestampNs;

        return frame;
    }

private:
    std::shared_ptr<timeline::Timeline> m_timeline;
    std::shared_ptr<timeline::Compositor> m_compositor;
    FrameBufferPool m_frameBufferPool;
};

} // namespace video
} // namespace sdk
