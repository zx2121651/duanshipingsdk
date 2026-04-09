#include "../include/FilterEngine.h"
#include <iostream>
#include <algorithm>

namespace sdk {
namespace video {

FilterEngine::FilterEngine() : m_initialized(false), m_simulateCrash(false) { m_shaderManager = std::make_shared<ShaderManager>(); }

FilterEngine::~FilterEngine() {
    release();
}

Result FilterEngine::initialize() {
    m_threadCheck.bind(); // Bind to current (GL) thread.

    // 初始化前首先唤醒嗅探器，对底层硬件进行深度体检
    m_contextManager.sniffCapabilities();

    for (auto& filter : m_filters) {
        filter->initialize();
    }

    m_initialized = true;
    return Result::ok();
}

ResultPayload<Texture> FilterEngine::processFrame(const Texture& textureIn, int width, int height) {
    if (!m_threadCheck.check("processFrame must be called on the render thread")) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "GL Thread violation detected during processFrame");
    }

    if (m_simulateCrash) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Simulated crash active");
    }

    if (!m_initialized) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "FilterEngine not initialized before processing frame");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    if (m_isGraphDirty) {
        buildCameraPipeline();
        m_isGraphDirty = false;
    }

    if (m_graph && m_outputNode) {
        if (m_cameraNode) {
            VideoFrame inFrame;
            inFrame.textureId = textureIn.id;
            inFrame.width = width;
            inFrame.height = height;
            inFrame.timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(start_time.time_since_epoch()).count();
            m_cameraNode->pushFrame(inFrame);
        }

        m_graph->execute(0);

        VideoFrame outFrame = m_outputNode->getLastFrame();

        auto end_time = std::chrono::high_resolution_clock::now();
        float duration_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
        m_metricsCollector.recordFrameTime(duration_ms);

        if (!outFrame.isValid()) {
            return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Pipeline executed but produced an invalid output texture");
        }
        return ResultPayload<Texture>::ok(Texture{outFrame.textureId, outFrame.width, outFrame.height});
    }

    return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Pipeline graph or output node is uninitialized");
}

void FilterEngine::updateParameter(const std::string& key, const std::any& value) {
    if (key == "simulateCrash") {
        try {
            float val = std::any_cast<float>(value);
            m_simulateCrash = (val > 0.5f);
        } catch (const std::bad_any_cast& e) { std::cerr << "updateParameter type cast error: " << e.what() << std::endl; }
        return;
    }

    for (auto& filter : m_filters) {
        filter->setParameter(key, value);
    }
}

void FilterEngine::release() {
    if (m_initialized) {
        for (auto& filter : m_filters) {
            filter->release();
        }
        m_frameBufferPool.clear();
        m_initialized = false;
    }
    m_filters.clear();
}

void FilterEngine::addFilterRaw(FilterPtr filter) {
    if (filter) {
        filter->setShaderManager(m_shaderManager);
        m_filters.push_back(filter);
        if (m_initialized) {
            filter->initialize();
            m_isGraphDirty = true;
        }
    }
}
void FilterEngine::addFilter(FilterType type) {
    FilterPtr filter = FilterFactory::createFilter(type, m_contextManager, &m_frameBufferPool);
    if (filter) {
        filter->setShaderManager(m_shaderManager);
        m_filters.push_back(filter);
        if (m_initialized) {
            filter->initialize();
            m_isGraphDirty = true;
        }
    }
}

void FilterEngine::removeAllFilters() {
    for (auto& filter : m_filters) {
        if (m_initialized) {
            filter->release();
        }
    }
    m_filters.clear();
    m_isGraphDirty = true;
}



void FilterEngine::updateShaderSource(const std::string& name, const std::string& source) {
    if (!m_shaderManager) return;

    m_shaderManager->updateShaderSource(name, source);

    // We need to notify all filters to reload their shaders if they use this one.
    // However, since we don't know which filter uses which shader name,
    // we should just call initialize() on all of them to trigger a recompilation.
    for (auto& filter : m_filters) {
        // filter->release();  // If we release, we lose other states. We should just re-init program.
        // Actually, we need a recompile API in Filter.
        filter->recompileProgram();
    }
}

void FilterEngine::buildCameraPipeline() {
    m_graph = std::make_shared<PipelineGraph>();
    m_cameraNode = std::make_shared<CameraInputNode>("Camera");
    m_outputNode = std::make_shared<OutputNode>("Display");

    m_graph->addNode(m_cameraNode);

    std::shared_ptr<PipelineNode> lastNode = m_cameraNode;

    for (size_t i = 0; i < m_filters.size(); ++i) {
        auto filterNode = std::make_shared<FilterNode>("Filter_" + std::to_string(i), m_filters[i]);

        // Restore FBOPrecision logic from legacy mode
        FBOPrecision targetPrecision = FBOPrecision::RGBA8888;
        bool isIntermediate = (i < m_filters.size() - 1);
        if (isIntermediate) {
            if (m_contextManager.isFP16RenderTargetSupported()) {
                targetPrecision = FBOPrecision::FP16;
            } else {
                targetPrecision = FBOPrecision::RGB565;
            }
        }
        filterNode->setFrameBufferPool(&m_frameBufferPool);
        filterNode->setTargetPrecision(targetPrecision);

        m_graph->addNode(filterNode);
        m_graph->connect(lastNode, filterNode);
        lastNode = filterNode;
    }

    m_graph->addNode(m_outputNode);
    m_graph->connect(lastNode, m_outputNode);
    Result res = m_graph->compile();
    if (!res.isOk()) { std::cerr << "Pipeline Compile Failed: " << res.getMessage() << std::endl; }
}

void FilterEngine::buildTimelinePipeline(std::shared_ptr<timeline::Timeline> timeline, std::shared_ptr<timeline::Compositor> compositor) {
    m_graph = std::make_shared<PipelineGraph>();
    auto timelineNode = std::make_shared<TimelineNode>("Timeline", timeline, compositor);
    m_outputNode = std::make_shared<OutputNode>("Display");

    m_graph->addNode(timelineNode);

    std::shared_ptr<PipelineNode> lastNode = timelineNode;

    for (size_t i = 0; i < m_filters.size(); ++i) {
        auto filterNode = std::make_shared<FilterNode>("Filter_" + std::to_string(i), m_filters[i]);

        // Restore FBOPrecision logic from legacy mode
        FBOPrecision targetPrecision = FBOPrecision::RGBA8888;
        bool isIntermediate = (i < m_filters.size() - 1);
        if (isIntermediate) {
            if (m_contextManager.isFP16RenderTargetSupported()) {
                targetPrecision = FBOPrecision::FP16;
            } else {
                targetPrecision = FBOPrecision::RGB565;
            }
        }
        filterNode->setFrameBufferPool(&m_frameBufferPool);
        filterNode->setTargetPrecision(targetPrecision);

        m_graph->addNode(filterNode);
        m_graph->connect(lastNode, filterNode);
        lastNode = filterNode;
    }

    m_graph->addNode(m_outputNode);
    m_graph->connect(lastNode, m_outputNode);
    Result res = m_graph->compile();
    if (!res.isOk()) { std::cerr << "Pipeline Compile Failed: " << res.getMessage() << std::endl; }
}

} // namespace video
} // namespace sdk
