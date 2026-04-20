#pragma once
#include "../core/include/FilterEngine.h"

namespace sdk {
namespace video {

class FilterEngineTestAccessor {
public:
    static bool isInitialized(const FilterEngine& engine) {
        return engine.m_initialized;
    }
    static bool isGraphDirty(const FilterEngine& engine) {
        return engine.m_isGraphDirty;
    }
    static std::shared_ptr<PipelineGraph> getGraph(const FilterEngine& engine) {
        return engine.m_graph;
    }
    static std::shared_ptr<CameraInputNode> getCameraNode(const FilterEngine& engine) {
        return engine.m_cameraNode;
    }
    static std::shared_ptr<OutputNode> getOutputNode(const FilterEngine& engine) {
        return engine.m_outputNode;
    }
    static MetricsCollector& getMetricsCollector(FilterEngine& engine) {
        return engine.m_metricsCollector;
    }

    static std::shared_ptr<rhi::IRenderDevice> getRenderDevice(const FilterEngine& engine) {
        return engine.m_renderDevice;
    }

    // Setters used by existing tests
    static void setOutputNode(FilterEngine& engine, std::shared_ptr<OutputNode> node) {
        engine.m_outputNode = node;
    }
    static void setInitialized(FilterEngine& engine, bool initialized) {
        engine.m_initialized = initialized;
    }
    static void setGraphDirty(FilterEngine& engine, bool dirty) {
        engine.m_isGraphDirty = dirty;
    }
};

} // namespace video
} // namespace sdk
