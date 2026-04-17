#pragma once
#include "GLTypes.h"
#include "Filter.h"
#include "FrameBufferPool.h"
#include "ThreadCheck.h"
#include "GLContextManager.h"
#include "pipeline/PipelineGraph.h"
#include "pipeline/Nodes.h"
#include "pipeline/TimelineNode.h"
#include "PerformanceMetrics.h"
#include <memory>
#include "FilterFactory.h"
#include <string>
#include <vector>
#include <any>
#include "rhi/IRenderDevice.h"

namespace sdk {
namespace video {

class FilterEngineTestAccessor;

class FilterEngine {
public:
    std::shared_ptr<ShaderManager> getShaderManager() const { return m_shaderManager; }
    void setAssetProvider(std::shared_ptr<IAssetProvider> provider) { m_shaderManager->setAssetProvider(provider); }

public:
    FilterEngine();
    ~FilterEngine();

    // Init context and setup standard filters. Call this on the GL thread.
    Result initialize();

    // Process a frame through the pipeline
    ResultPayload<Texture> processFrame(const Texture& textureIn, int width, int height);

    // Update filter parameters
    void updateParameter(const std::string& key, const std::any& value);
    void updateParameterMat4(const std::string& key, const float* matrix);

    // Release resources
    void release();

    // Graph Builder APIs
    Result buildCameraPipeline();
    Result buildTimelinePipeline(std::shared_ptr<timeline::Timeline> timeline, std::shared_ptr<timeline::Compositor> compositor);

    // Pipeline manipulation
        Result addFilter(FilterType type);
    Result addFilterRaw(FilterPtr filter); // Expose for internal nodes like OES2RGB
    Result removeAllFilters();

    PerformanceMetrics getPerformanceMetrics() const { return m_metricsCollector.getMetrics(); }
    void recordDroppedFrame() { m_metricsCollector.recordDroppedFrame(); }
    Result updateShaderSource(const std::string& name, const std::string& source);


    // 暴露 FBO 内存池供子滤镜（如 Two-pass 高斯模糊）借用
    FrameBufferPool* getFrameBufferPool() { return &m_frameBufferPool; }

    // 暴露 ContextManager 给 NativeBridge 探测
    GLContextManager& getContextManager() { return m_contextManager; }

    // Test access
    friend class FilterEngineTestAccessor;

private:
    FrameBufferPool m_frameBufferPool;
    std::shared_ptr<ShaderManager> m_shaderManager;


    bool m_isGraphDirty = true;
    std::shared_ptr<PipelineGraph> m_graph;
    std::shared_ptr<CameraInputNode> m_cameraNode;
    std::shared_ptr<OutputNode> m_outputNode;

    ThreadCheck m_threadCheck;
    bool m_initialized;
    mutable MetricsCollector m_metricsCollector;


    // 【三级防线】特征级嗅探器
    GLContextManager m_contextManager;

    // RHI 设备实例
    std::shared_ptr<rhi::IRenderDevice> m_renderDevice;

    Result rebuildGraph(std::shared_ptr<PipelineNode> inputNode);
};

using FilterEnginePtr = std::shared_ptr<FilterEngine>;

} // namespace video
} // namespace sdk
