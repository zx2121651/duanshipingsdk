#pragma once

#include "PipelineNode.h"
#include "../Filter.h"
#include "../FrameBufferPool.h"
#include <mutex>

namespace sdk {
namespace video {

// 1. Source Node: Simulates Camera/OES pushing frames into the pipeline
class CameraInputNode : public PipelineNode {
public:
    CameraInputNode(const std::string& name) : PipelineNode(name) {}

    // Called by the host platform to push a new frame
    void pushFrame(const VideoFrame& frame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestFrame = frame;
    }

    ResultPayload<VideoFrame> pullFrame(int64_t timestampNs) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Note: For advanced sync, we'd compare timestampNs to m_latestFrame.timestampNs
        if (!m_latestFrame.isValid()) {
            return ResultPayload<VideoFrame>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "CameraInputNode has no valid frame");
        }
        return ResultPayload<VideoFrame>::ok(m_latestFrame);
    }

private:
    std::mutex m_mutex;
    VideoFrame m_latestFrame;
};

// 2. Filter Node: Wraps an existing legacy Filter
class FilterNode : public PipelineNode {
public:
    FilterNode(const std::string& name, std::shared_ptr<Filter> filter)
        : PipelineNode(name), m_filter(filter) {}

    std::shared_ptr<Filter> getFilter() const { return m_filter; }
    Result initialize() override {
        if (m_filter) return m_filter->initialize();
        return Result::ok();
    }

    void release() override {
        if (m_filter) m_filter->release();
    }

    ResultPayload<VideoFrame> pullFrame(int64_t timestampNs) override {
        if (m_inputs.empty()) {
            return ResultPayload<VideoFrame>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterNode has no input");
        }
        if (!m_filter) {
            return ResultPayload<VideoFrame>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterNode has no filter");
        }

        // Pull from upstream
        auto upstreamRes = m_inputs[0]->pullFrame(timestampNs);
        if (!upstreamRes.isOk()) return upstreamRes;

        VideoFrame upstreamFrame = upstreamRes.getValue();
        if (!upstreamFrame.isValid()) {
            return ResultPayload<VideoFrame>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Upstream produced an invalid frame");
        }

        Texture texIn{upstreamFrame.textureId, upstreamFrame.width, upstreamFrame.height};

        // Allocate a frame buffer from the pool attached to this node (if provided)
        FrameBufferPtr fbo = nullptr;
        if (m_pool) {
            fbo = m_pool->getFrameBuffer(upstreamFrame.width, upstreamFrame.height, m_targetPrecision);
        }

        auto processRes = m_filter->processFrame(texIn, fbo);
        if (!processRes.isOk()) {
            return ResultPayload<VideoFrame>::error(processRes.getErrorCode(), processRes.getMessage());
        }
        Texture texOut = processRes.getValue();

        VideoFrame outFrame;
        outFrame.textureId = texOut.id;
        outFrame.width = texOut.width;
        outFrame.height = texOut.height;
        outFrame.timestampNs = upstreamFrame.timestampNs;
        outFrame.transformMatrix = upstreamFrame.transformMatrix;
        // Retain the FBO so it is returned to the pool only when this VideoFrame is destroyed
        outFrame.frameBuffer = fbo;

        return ResultPayload<VideoFrame>::ok(outFrame);
    }

    void setFrameBufferPool(FrameBufferPool* pool) { m_pool = pool; }
    void setTargetPrecision(FBOPrecision precision) { m_targetPrecision = precision; }

private:
    std::shared_ptr<Filter> m_filter;
    FrameBufferPool* m_pool = nullptr;
    FBOPrecision m_targetPrecision = FBOPrecision::RGBA8888;
};

// 3. Sink Node: Collects the final output
class OutputNode : public PipelineNode {
public:
    OutputNode(const std::string& name) : PipelineNode(name) {}

    ResultPayload<VideoFrame> pullFrame(int64_t timestampNs) override {
        if (m_inputs.empty()) {
            return ResultPayload<VideoFrame>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "OutputNode has no input");
        }

        auto res = m_inputs[0]->pullFrame(timestampNs);
        if (res.isOk()) {
            m_lastFrame = res.getValue();
        }
        return res;
    }

    VideoFrame getLastFrame() const { return m_lastFrame; }

private:
    VideoFrame m_lastFrame;
};

} // namespace video
} // namespace sdk
