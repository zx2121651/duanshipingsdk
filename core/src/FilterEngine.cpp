#include "../include/FilterEngine.h"
#define LOG_TAG "FilterEngine"
#include "../include/Log.h"
#include "rhi/GLRenderDevice.h"
#include <iostream>
#include <chrono>
#include <algorithm>

namespace sdk {
namespace video {

FilterEngine::FilterEngine() : m_initialized(false) { m_shaderManager = std::make_shared<ShaderManager>(); }

FilterEngine::~FilterEngine() {
    release();
}

Result FilterEngine::initialize() {
    if (m_initialized) {
        // Idempotent Path: Ensure we are still on the same thread that first initialized the engine.
        // This maintains the "binding" semantics.
        if (!m_threadCheck.check("Repeated initialize() must be called on the same thread")) {
            return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine already initialized on another thread");
        }
        LOGI("FilterEngine::initialize() - Already initialized, skipping.");
        return Result::ok();
    }

    // Thread Safety: Bind this engine instance to the caller's thread (Render Thread).
    // All subsequent GPU-related calls must originate from this thread.
    m_threadCheck.bind();

    // 初始化前首先唤醒嗅探器，对底层硬件进行深度体检
    m_contextManager.sniffCapabilities();

    // 实例化 RHI 后端设备 (当前过渡阶段始终使用 GLRenderDevice)
    m_renderDevice = std::make_shared<rhi::GLRenderDevice>();

    if (m_graph) {
        for (const auto& node : m_graph->getNodes()) {
            node->initialize();
        }
    }

    m_initialized = true;
    return Result::ok();
}

ResultPayload<Texture> FilterEngine::processFrame(const Texture& textureIn, int width, int height) {
    if (!m_initialized) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine not initialized before processing frame");
    }

    // Thread Safety: Verify that we are still on the Render Thread.
    if (!m_threadCheck.check("processFrame must be called on the render thread")) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "GL Thread violation detected during processFrame");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    if (m_isGraphDirty) {
        Result res = buildCameraPipeline();
        if (!res.isOk()) {
            return ResultPayload<Texture>::error(res.getErrorCode(), "Pipeline build failed: " + res.getMessage());
        }
        m_isGraphDirty = false;
    }

    if (m_graph && m_outputNode) {
        LOGD("Processing frame: input %u, size %dx%d", textureIn.id, width, height);
        if (m_cameraNode) {
            VideoFrame inFrame;
            inFrame.textureId = textureIn.id;
            inFrame.width = width;
            inFrame.height = height;
            inFrame.timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(start_time.time_since_epoch()).count();
            m_cameraNode->pushFrame(inFrame);
        }

        Result execRes = m_graph->execute(0);
        if (!execRes.isOk()) {
            LOGE("Graph execution failed: %s", execRes.getMessage().c_str());
            return ResultPayload<Texture>::error(execRes.getErrorCode(), execRes.getMessage());
        }

        VideoFrame outFrame = m_outputNode->getLastFrame();

        auto end_time = std::chrono::high_resolution_clock::now();
        float duration_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
        m_metricsCollector.recordFrameTime(duration_ms);

        if (!outFrame.isValid()) {
            LOGE("Pipeline produced an invalid output frame (missing texture)");
            return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Pipeline produced an invalid output frame (missing texture)");
        }
        return ResultPayload<Texture>::ok(Texture{outFrame.textureId, outFrame.width, outFrame.height});
    }

    return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Pipeline graph or output node is uninitialized");
}

void FilterEngine::updateParameter(const std::string& key, const std::any& value) {
    if (!m_initialized) return;
    if (m_graph) {
        for (const auto& node : m_graph->getNodes()) {
            auto filterNode = std::dynamic_pointer_cast<FilterNode>(node);
            if (filterNode && filterNode->getFilter()) {
                filterNode->getFilter()->setParameter(key, value);
            }
        }
    }
}

void FilterEngine::updateParameterMat4(const std::string& key, const float* matrix) {
    if (!m_initialized) return;
    if (m_graph) {
        for (const auto& node : m_graph->getNodes()) {
            auto filterNode = std::dynamic_pointer_cast<FilterNode>(node);
            if (filterNode && filterNode->getFilter()) {
                filterNode->getFilter()->setParameterMat4(key, matrix);
            }
        }
    }
}

void FilterEngine::release() {
    if (!m_initialized) {
        return;
    }

    // 1. Graph release and reset
    if (m_graph) {
        m_graph->release();
        m_graph = nullptr;
    }

    // 2. Node reference nullification
    m_cameraNode = nullptr;
    m_outputNode = nullptr;

    // 3. RHI device nullification
    m_renderDevice = nullptr;

    // 4. FrameBufferPool and MetricsCollector clearing
    m_frameBufferPool.clear();
    m_metricsCollector.clear();

    // 5. State flags reset
    m_isGraphDirty = true;
    m_initialized = false;

    // 6. ThreadCheck unbinding
    m_threadCheck.unbind();
}

Result FilterEngine::addFilterRaw(FilterPtr filter) {
    if (!m_initialized) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine not initialized");
    }
    if (filter) {
        filter->setShaderManager(m_shaderManager);
        if (!m_graph) { m_graph = std::make_shared<PipelineGraph>(); }
        auto filterNode = std::make_shared<FilterNode>("Filter_" + std::to_string(m_graph->getNodes().size()), filter);
        m_graph->addNode(filterNode);
        if (m_initialized) {
            auto res = filter->initialize();
            if (!res.isOk()) return res;
        }
        m_isGraphDirty = true;
        return Result::ok();
    }
    return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Filter is null");
}
Result FilterEngine::addFilter(FilterType type) {
    if (!m_initialized) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine not initialized");
    }
    // We must have a render device to create filters
    if (!m_renderDevice) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Render device is null");
    }
    FilterPtr filter = FilterFactory::createFilter(type, m_contextManager, &m_frameBufferPool, m_renderDevice);
    if (filter) {
        return addFilterRaw(filter);
    }
    return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED, "Failed to create filter from factory");
}

Result FilterEngine::removeAllFilters() {
    if (!m_initialized) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine not initialized");
    }
    if (m_graph) {
        m_graph->release();
        m_graph = std::make_shared<PipelineGraph>();
    }
    m_isGraphDirty = true;
    return Result::ok();
}



Result FilterEngine::updateShaderSource(const std::string& name, const std::string& source) {
    if (!m_initialized) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine not initialized");
    }
    if (!m_shaderManager) return Result::error("ShaderManager is null");

    m_shaderManager->updateShaderSource(name, source);

    // We need to notify all filters to reload their shaders if they use this one.
    // However, since we don't know which filter uses which shader name,
    // we should just call initialize() on all of them to trigger a recompilation.
    if (m_graph) {
        for (const auto& node : m_graph->getNodes()) {
            auto filterNode = std::dynamic_pointer_cast<FilterNode>(node);
            if (filterNode && filterNode->getFilter()) {
                auto res = filterNode->getFilter()->recompileProgram();
                if (!res.isOk()) return res;
            }
        }
    }
    return Result::ok();
}

Result FilterEngine::buildCameraPipeline() {
    if (!m_initialized) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine not initialized");
    }
    auto cameraNode = std::make_shared<CameraInputNode>("Camera");
    return rebuildGraph(cameraNode);
}

Result FilterEngine::buildTimelinePipeline(std::shared_ptr<timeline::Timeline> timeline, std::shared_ptr<timeline::Compositor> compositor) {
    if (!m_initialized) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine not initialized");
    }
    auto timelineNode = std::make_shared<TimelineNode>("Timeline", timeline, compositor);
    return rebuildGraph(timelineNode);
}

Result FilterEngine::rebuildGraph(std::shared_ptr<PipelineNode> inputNode) {
    // Transactional Graph Rebuild:
    // We build the new graph in isolation and only commit it to the engine state
    // if compilation succeeds.

    auto newOutputNode = std::make_shared<OutputNode>("Display");
    auto newCameraNode = std::dynamic_pointer_cast<CameraInputNode>(inputNode);

    if (auto timelineNode = std::dynamic_pointer_cast<TimelineNode>(inputNode)) {
        timelineNode->setFrameBufferPool(&m_frameBufferPool);
    }

    std::shared_ptr<PipelineNode> lastNode = inputNode;

    // Create new graph
    auto newGraph = std::make_shared<PipelineGraph>();
    newGraph->addNode(inputNode);
    newGraph->addNode(newOutputNode);

    // Keep existing filters
    if (m_graph) {
        std::vector<std::shared_ptr<Filter>> rawFilters;
        for (const auto& node : m_graph->getNodes()) {
            if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(node)) {
                rawFilters.push_back(filterNode->getFilter());
            }
        }

        for (size_t i = 0; i < rawFilters.size(); ++i) {
            auto filterNode = std::make_shared<FilterNode>("Filter_" + std::to_string(i), rawFilters[i]);

            FBOPrecision targetPrecision = FBOPrecision::RGBA8888;
            bool isIntermediate = (i < rawFilters.size() - 1);
            if (isIntermediate) {
                if (m_contextManager.isFP16RenderTargetSupported()) {
                    targetPrecision = FBOPrecision::FP16;
                } else {
                    targetPrecision = FBOPrecision::RGB565;
                }
            }
            filterNode->setFrameBufferPool(&m_frameBufferPool);
            filterNode->setTargetPrecision(targetPrecision);

            newGraph->addNode(filterNode);
            newGraph->connect(lastNode, filterNode);
            lastNode = filterNode;
        }
    }

    newGraph->connect(lastNode, newOutputNode);

    // Verify the new topology before committing
    Result res = newGraph->compile();
    if (!res.isOk()) {
        LOGE("Pipeline Compile Failed during rebuild: %s", res.getMessage().c_str());
        return res;
    }

    LOGI("Pipeline rebuilt successfully with %zu nodes", newGraph->getNodes().size());

    // Atomic commit of the new state
    if (m_graph) {
        m_graph->release();
    }
    m_graph = newGraph;
    m_outputNode = newOutputNode;
    m_cameraNode = newCameraNode;

    return Result::ok();
}

} // namespace video
} // namespace sdk
