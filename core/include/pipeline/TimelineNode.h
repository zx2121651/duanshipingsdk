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

    ResultPayload<VideoFrame> pullFrame(int64_t timestampNs) override {
        if (!m_timeline || !m_compositor) {
            return ResultPayload<VideoFrame>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "TimelineNode not properly initialized");
        }

        // Convert ns to ms or internal timeline units
        int64_t timeNs = timestampNs;

        int width = m_timeline->getOutputWidth();
        int height = m_timeline->getOutputHeight();

        FrameBufferPtr fbo = m_frameBufferPool.getFrameBuffer(width, height);
        if (!fbo) {
            return ResultPayload<VideoFrame>::error(ErrorCode::ERR_RENDER_FBO_ALLOC_FAILED, "Failed to allocate FBO for TimelineNode");
        }

        Result res = m_compositor->renderFrameAtTime(timeNs, fbo);
        if (!res.isOk()) {
            return ResultPayload<VideoFrame>::error(res.getErrorCode(), res.getMessage());
        }

        VideoFrame frame;
        frame.textureId = fbo->getTexture().id;
        frame.width = fbo->getTexture().width;
        frame.height = fbo->getTexture().height;
        frame.frameBuffer = fbo;
        frame.timestampNs = timestampNs;

        return ResultPayload<VideoFrame>::ok(frame);
    }

private:
    std::shared_ptr<timeline::Timeline> m_timeline;
    std::shared_ptr<timeline::Compositor> m_compositor;
    FrameBufferPool m_frameBufferPool;
};

} // namespace video
} // namespace sdk
