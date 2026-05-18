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
#include "rhi/RenderDeviceFactory.h"

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
     * @brief Configure Dynamic Resolution Scaling (DSR) for Timeline mode.
     */
    void setDsrConfig(float targetFps, float minScaleFactor = 0.5f, float maxScaleFactor = 1.0f);
    void disableDsr();

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

    /**
     * @brief P1-6: Call when the EGL/EAGL context is lost (e.g. app backgrounded, driver eviction).
     * Releases all CPU-side GL state bookkeeping. All GPU handles are assumed dead.
     * @note Must be called on the Render thread.
     */
    void onContextLost();

    /**
     * @brief P1-6: Call after a new context has been made current on the Render thread.
     * Re-initializes the engine and rebuilds the pipeline on the new context.
     * @return Result::ok() on success, error code otherwise.
     */
    Result onContextRestored();

    /**
     * @brief P2-14: Android ComponentCallbacks2::onTrimMemory hook.
     *
     * 根据系统内存压力等级分五档清理 GPU FBO 缓存：
     *  >= 80 (COMPLETE)   → 清空所有 FBO，VRAM 上限降至  32 MB
     *  >= 40 (BACKGROUND) → 清空所有 FBO，VRAM 上限降至  64 MB
     *  >= 15 (RUNNING_CRITICAL) → VRAM 上限降至  64 MB（下次分配触发驱逐）
     *  >= 10 (RUNNING_LOW)      → VRAM 上限降至 128 MB
     *  >=  5 (RUNNING_MODERATE) → VRAM 上限降至 192 MB
     *  UI_HIDDEN (20)    → 视同 BACKGROUND 处理
     *
     * @note Thread-safe（内部加锁）。可在任意线程调用。
     * @param level  ComponentCallbacks2 常量值
     */
    void onTrimMemory(int level);


    // 暴露 FBO 内存池供子滤镜（如 Two-pass 高斯模糊）借用
    FrameBufferPool* getFrameBufferPool() { return &m_frameBufferPool; }

    // 暴露 ContextManager 给 NativeBridge 探测
    GLContextManager& getContextManager() { return m_contextManager; }

    // 暴露 RHI 设备供 Compositor / 其他子系统创建 shader program
    rhi::IRenderDevice* getRenderDevice() { return m_renderDevice.get(); }

    // RHI 后端选择 API
    void setPreferredBackend(rhi::BackendType type) { m_preferredBackend = type; }
    rhi::BackendType getPreferredBackend() const { return m_preferredBackend; }
    rhi::BackendType getActiveBackend()    const { return m_activeBackend; }

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

    // 后端类型（用户首选 + 实际选择）
    rhi::BackendType m_preferredBackend = rhi::BackendType::AUTO;
    rhi::BackendType m_activeBackend    = rhi::BackendType::GLES;

    Result rebuildGraph(std::shared_ptr<PipelineNode> inputNode);
};

using FilterEnginePtr = std::shared_ptr<FilterEngine>;

} // namespace video
} // namespace sdk
