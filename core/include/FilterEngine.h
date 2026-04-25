#include "rhi/IVertexArray.h"
#include "rhi/IBuffer.h"
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
    std::shared_ptr<rhi::IVertexArray> getQuadVao() const { return m_quadVao; }
private:
    std::shared_ptr<rhi::IVertexArray> m_quadVao;
    std::shared_ptr<rhi::IBuffer> m_quadVbo;
public:
    std::shared_ptr<ShaderManager> getShaderManager() const { return m_shaderManager; }
    void setAssetProvider(std::shared_ptr<IAssetProvider> provider) { m_shaderManager->setAssetProvider(provider); }

public:
    FilterEngine();
    ~FilterEngine();

    /**
     * @brief Initialize the engine.
     * @note MUST be called on the GL/Render thread. This call binds the engine to the current thread.
     */
    Result initialize();

    /**
     * @brief Process an input texture through the filter pipeline.
     * @note MUST be called on the Render thread bound during initialize().
     */
    ResultPayload<Texture> processFrame(const Texture& textureIn, int width, int height);

    /**
     * @brief Update filter parameters.
     * @note Thread-safe if called on Render thread.
     */
    void updateParameter(const std::string& key, const std::any& value);
    void updateParameterMat4(const std::string& key, const float* matrix);

    /**
     * @brief Release all GPU resources.
     * @note MUST be called on the Render thread to ensure proper OpenGL resource cleanup.
     */
    void release();

    // Graph Builder APIs (Internal use, should be called on Render thread)
    Result buildCameraPipeline();
    Result buildTimelinePipeline(std::shared_ptr<timeline::Timeline> timeline, std::shared_ptr<timeline::Compositor> compositor);

    /**
     * @brief Add a filter to the pipeline.
     * @note MUST be called on the Render thread as it triggers filter initialization.
     */
    Result addFilter(FilterType type);
    Result addFilterRaw(FilterPtr filter);

    /**
     * @brief Remove all filters and reset the pipeline.
     * @note MUST be called on the Render thread.
     */
    Result removeAllFilters();

    PerformanceMetrics getPerformanceMetrics() const { return m_metricsCollector.getMetrics(); }
    void recordDroppedFrame() { if (m_initialized) m_metricsCollector.recordDroppedFrame(); }
    Result updateShaderSource(const std::string& name, const std::string& source);


    // 暴露 FBO 内存池供子滤镜（如 Two-pass 高斯模糊）借用
    FrameBufferPool* getFrameBufferPool() { return &m_frameBufferPool; }

    // 暴露 ContextManager 给 NativeBridge 探测
    GLContextManager& getContextManager() { return m_contextManager; }

    // Test access
    friend class FilterEngineTestAccessor;

    enum class PipelineMode {
        UNDEFINED,
        CAMERA,
        TIMELINE
    };

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

    PipelineMode m_currentMode = PipelineMode::UNDEFINED;
    std::shared_ptr<timeline::Timeline> m_timeline;
    std::weak_ptr<timeline::Compositor> m_compositor;


    // 【三级防线】特征级嗅探器
    GLContextManager m_contextManager;

    // RHI 设备实例
    std::shared_ptr<rhi::IRenderDevice> m_renderDevice;

    Result rebuildGraph(std::shared_ptr<PipelineNode> inputNode);
};

using FilterEnginePtr = std::shared_ptr<FilterEngine>;

} // namespace video
} // namespace sdk
