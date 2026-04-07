#pragma once

#include "PipelineNode.h"
#include "../timeline/Timeline.h"
#include "../timeline/Compositor.h"

namespace sdk {
namespace video {

class TimelineNode : public PipelineNode {
public:
    TimelineNode(const std::string& name, std::shared_ptr<Timeline> timeline, std::shared_ptr<Compositor> compositor)
        : PipelineNode(name), m_timeline(timeline), m_compositor(compositor) {}

    VideoFrame pullFrame(int64_t timestampNs) override {
        if (!m_timeline || !m_compositor) return VideoFrame();

        // Convert ns to ms or internal timeline units
        int64_t timeMs = timestampNs / 1000000;

        // Ask compositor to render the frame at this specific time.
        // The Compositor internally pulls from DecoderPool and blends clips.
        Texture outTex = m_compositor->renderFrameAtTime(m_timeline, timeMs);

        VideoFrame frame;
        frame.textureId = outTex.id;
        frame.width = outTex.width;
        frame.height = outTex.height;
        frame.timestampNs = timestampNs;

        return frame;
    }

private:
    std::shared_ptr<Timeline> m_timeline;
    std::shared_ptr<Compositor> m_compositor;
};

} // namespace video
} // namespace sdk
