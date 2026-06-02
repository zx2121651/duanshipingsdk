#include "../include/FilterEngine.h"
#define LOG_TAG "FilterEngine"
#include "../include/Log.h"

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#elif defined(__ANDROID__)
    #include <GLES3/gl3.h>
#else
    #include "GLES3/gl3.h"
#endif

#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif

#include "rhi/GLRenderDevice.h"
#include "../include/rhi/RenderDeviceFactory.h"
#include "../include/GLStateManager.h"
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
            return Result::error(ErrorCode::ERR_RENDER_THREAD_VIOLATION, "FilterEngine already initialized on another thread");
        }
        LOGI("FilterEngine::initialize() - Already initialized, skipping.");
        return Result::ok();
    }

    // Thread Safety: Bind this engine instance to the caller's thread (Render Thread).
    // All subsequent GPU-related calls must originate from this thread.
    m_threadCheck.bind();

    // 初始化前首先唤醒嗅探器，对底层硬件进行深度体检
    m_contextManager.sniffCapabilities();

    // 通过 RenderDeviceFactory 按首选后端 + 设备能力自动选择 RHI 实现
    m_renderDevice = rhi::RenderDeviceFactory::create(m_preferredBackend, m_contextManager, m_activeBackend);
    LOGI("FilterEngine: RHI backend = %s, GLES tier = %d",
         rhi::backendTypeName(m_activeBackend),
         m_contextManager.getGLESVersionInt());

    // Create Global Quad VAO/VBO via RHI
    static const float s_quadVertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, // pos(2), uv(2)
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };
    m_quadVbo = m_renderDevice->createBuffer(rhi::BufferType::VertexBuffer, rhi::BufferUsage::StaticDraw, sizeof(s_quadVertices), s_quadVertices);
    m_quadVao = m_renderDevice->createVertexArray();
    m_quadVao->addVertexBuffer(m_quadVbo, {
        {0, rhi::VertexFormat::Float2, 0, 4 * sizeof(float)}, // Location 0: position
        {1, rhi::VertexFormat::Float2, 2 * sizeof(float), 4 * sizeof(float)} // Location 1: texcoord
    });


    if (m_graph) {
        for (const auto& node : m_graph->getNodes()) {
            node->initialize();
        }
    }

    // [FIX] Drain any GL errors accumulated during initialization (sniffCapabilities,
    // VAO/VBO creation, shader compilation).  If left uncleared, these errors will
    // surface during the next SurfaceTexture.updateTexImage() call on the Kotlin side,
    // causing GL_INVALID_OPERATION (0x502) and a black screen.
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            LOGW("FilterEngine::initialize() draining stale GL error(s) from init chain: 0x%x", err);
            int drainCount = 1;
            while ((err = glGetError()) != GL_NO_ERROR && drainCount < 16) {
                LOGW("  additional stale GL error: 0x%x", err);
                drainCount++;
            }
        }
    }

    m_initialized = true;
    return Result::ok();
}

ResultPayload<Texture> FilterEngine::processFrame(const Texture& textureIn, int width, int height) {
    if (!m_initialized) {
        LOGE("processFrame() called before initialize(). Did you forget to call initialize()?");
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_NOT_INITIALIZED, "FilterEngine not initialized before processing frame");
    }

    // Thread Safety: Verify that we are still on the Render Thread.
    if (!m_threadCheck.check("processFrame must be called on the render thread")) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_THREAD_VIOLATION, "GL Thread violation detected during processFrame");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    if (m_isGraphDirty) {
        Result res = Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Pipeline not configured");
        if (m_currentMode == PipelineMode::CAMERA) {
            LOGI("Auto-rebuilding Camera pipeline...");
            res = buildCameraPipeline();
        } else if (m_currentMode == PipelineMode::TIMELINE) {
            LOGI("Auto-rebuilding Timeline pipeline...");
            if (auto compositor = m_compositor.lock()) {
                auto timelineNode = std::make_shared<TimelineNode>("Timeline", m_timeline, compositor);
                res = rebuildGraph(timelineNode);
            } else {
                res = Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Compositor already released");
            }
        } else {
            LOGE("processFrame() failed: No pipeline mode established. Call buildCameraPipeline() or buildTimelinePipeline() first.");
        }

        if (!res.isOk()) {
            return ResultPayload<Texture>::error(res.getErrorCode(), "Pipeline auto-rebuild failed: " + res.getMessage());
        }

        // Post-rebuild error check
        GLenum postRebuildErr = glGetError();
        if (postRebuildErr != GL_NO_ERROR) {
            LOGE("FilterEngine: GL Error detected after pipeline rebuild: 0x%x", postRebuildErr);
        }

        m_isGraphDirty = false;
    }

    if (m_graph && m_outputNode) {
        LOGD("Processing frame: input %u, size %dx%d", textureIn.id, width, height);

        // Task 2: Insert a rigorous pre-check glGetError() block right before sync buffers are swapped or SurfaceTexture updates.
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            LOGE("FilterEngine: GL Error detected BEFORE frame processing: 0x%x. Resetting state...", err);

            // Task 2: Dump the shader pipeline uniform compilation states
            for (const auto& node : m_graph->getNodes()) {
                if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(node)) {
                    auto filter = filterNode->getFilter();
                    if (filter) {
                        LOGW("Node %s: programId %u",
                             filterNode->getName().c_str(),
                             filter->getProgramId());
                    }
                }
            }

            // Task 2: Reset the active bound texture unit status instead of allowing it to blind downstream FBO drawing nodes.
            for (int i = 0; i < 4; ++i) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, 0);
                glBindTexture(0x8D65 /* GL_TEXTURE_EXTERNAL_OES */, 0);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glUseProgram(0);
        }

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
            LOGE("Graph execution failed [Error: %d]: %s", execRes.getErrorCode(), execRes.getMessage().c_str());
            return ResultPayload<Texture>::error(execRes.getErrorCode(), execRes.getMessage());
        }

        // [FIX] Drain GL errors produced by graph execution (shader ops,
        // FBO binds, sampler rebinds on Mali/Adreno).  Without this drain,
        // GL_INVALID_OPERATION (0x502) from execute() persists into the next
        // frame's pre-check and floods logcat with per-frame warnings on the
        // Kotlin side even though the Kotlin drain runs before updateTexImage.
        {
            GLenum postExecErr = glGetError();
            if (postExecErr != GL_NO_ERROR) {
                LOGW("processFrame: post-execute GL error(s) drained:");
                do {
                    LOGW("  0x%x", postExecErr);
                    postExecErr = glGetError();
                } while (postExecErr != GL_NO_ERROR);
            }
        }

        VideoFrame outFrame = m_outputNode->getLastFrame();

        auto end_time = std::chrono::high_resolution_clock::now();
        float duration_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
        m_metricsCollector.recordFrameTime(duration_ms);

        if (!outFrame.isValid()) {
            LOGE("Pipeline produced an invalid output frame (missing texture)");
            return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Pipeline produced an invalid output frame");
        }

        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pendingFrames[outFrame.textureId] = outFrame;
            m_pendingFrameIds.push_back(outFrame.textureId);
            while (m_pendingFrameIds.size() > 3) {
                uint32_t oldestId = m_pendingFrameIds.front();
                m_pendingFrameIds.erase(m_pendingFrameIds.begin());
                m_pendingFrames.erase(oldestId);
            }
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
    // 强制先释放所有由逻辑设备创建出的 RHI 依赖资源
    m_quadVao = nullptr;
    m_quadVbo = nullptr;

    // 1. Graph release and reset
    if (m_graph) {
        m_graph->release();
        m_graph = nullptr;
    }

    // 2. Node reference nullification
    m_cameraNode = nullptr;
    m_outputNode = nullptr;
    m_pendingFrames.clear();
    m_pendingFrameIds.clear();

    // 3. RHI device nullification (这会析构 VulkanRenderDevice，销毁逻辑设备句柄)
    m_renderDevice = nullptr;

    // 4. FrameBufferPool and MetricsCollector clearing
    m_frameBufferPool.clear();
    m_metricsCollector.clear();

    // 5. State flags reset
    m_isGraphDirty = true;
    m_initialized = false;
    m_currentMode = PipelineMode::UNDEFINED;
    m_timeline = nullptr;
    m_compositor.reset();

    // 6. ThreadCheck unbinding
    m_threadCheck.unbind();
}

Result FilterEngine::addFilterRaw(FilterPtr filter) {
    if (!m_initialized) {
        LOGE("addFilterRaw() called before initialize()");
        return Result::error(ErrorCode::ERR_RENDER_NOT_INITIALIZED, "FilterEngine not initialized");
    }
    if (filter) {
        filter->setShaderManager(m_shaderManager);
        filter->setRenderDevice(m_renderDevice);
        filter->setQuadVao(m_quadVao);
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
        LOGE("addFilter() called before initialize()");
        return Result::error(ErrorCode::ERR_RENDER_NOT_INITIALIZED, "FilterEngine not initialized");
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
        LOGE("removeAllFilters() called before initialize()");
        return Result::error(ErrorCode::ERR_RENDER_NOT_INITIALIZED, "FilterEngine not initialized");
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
        LOGE("updateShaderSource() called before initialize()");
        return Result::error(ErrorCode::ERR_RENDER_NOT_INITIALIZED, "FilterEngine not initialized");
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
        LOGE("buildCameraPipeline() called before initialize()");
        return Result::error(ErrorCode::ERR_RENDER_NOT_INITIALIZED, "FilterEngine not initialized");
    }

    // Camera pipeline must always start with an OES2RGBFilter to convert
    // GL_TEXTURE_EXTERNAL_OES into a regular GL_TEXTURE_2D that downstream
    // filters and the Android display layer (sampler2D) can sample.
    bool hasOESFilter = false;
    if (m_graph) {
        for (const auto& node : m_graph->getNodes()) {
            if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(node)) {
                if (std::dynamic_pointer_cast<OES2RGBFilter>(filterNode->getFilter())) {
                    hasOESFilter = true;
                    break;
                }
            }
        }
    }
    if (!hasOESFilter) {
        LOGW("OES2RGBFilter NOT FOUND in current graph. Adding it automatically.");
        auto oesFilter = std::make_shared<OES2RGBFilter>();
        oesFilter->setShaderManager(m_shaderManager);
        oesFilter->setRenderDevice(m_renderDevice);
        oesFilter->setQuadVao(m_quadVao);
        auto initRes = oesFilter->initialize();
        if (!initRes.isOk()) {
            LOGE("Failed to initialize OES2RGBFilter: %s", initRes.getMessage().c_str());
            return initRes;
        }
        if (!m_graph) {
            m_graph = std::make_shared<PipelineGraph>();
        }
        auto oesNode = std::make_shared<FilterNode>("OES2RGB", oesFilter);
        oesNode->setFrameBufferPool(&m_frameBufferPool);
        oesNode->setTargetPrecision(FBOPrecision::RGBA8888);
        m_graph->addNode(oesNode);
    }

    auto cameraNode = std::make_shared<CameraInputNode>("Camera");
    Result res = rebuildGraph(cameraNode);
    if (res.isOk()) {
        m_currentMode = PipelineMode::CAMERA;
        m_timeline = nullptr;
        m_compositor.reset();
    }
    return res;
}

Result FilterEngine::buildTimelinePipeline(std::shared_ptr<timeline::Timeline> timeline, std::shared_ptr<timeline::Compositor> compositor) {
    if (!m_initialized) {
        LOGE("buildTimelinePipeline() called before initialize()");
        return Result::error(ErrorCode::ERR_RENDER_NOT_INITIALIZED, "FilterEngine not initialized");
    }
    auto timelineNode = std::make_shared<TimelineNode>("Timeline", timeline, compositor);
    Result res = rebuildGraph(timelineNode);
    if (res.isOk()) {
        m_currentMode = PipelineMode::TIMELINE;
        m_timeline = timeline;
        m_compositor = compositor;
    }
    return res;
}

void FilterEngine::setDsrConfig(float targetFps, float minScaleFactor, float maxScaleFactor) {
    if (auto comp = m_compositor.lock()) {
        timeline::DsrConfig cfg;
        cfg.targetFps = targetFps;
        cfg.minScaleFactor = minScaleFactor;
        cfg.maxScaleFactor = maxScaleFactor;
        comp->setDsrConfig(cfg);
    }
}

void FilterEngine::disableDsr() {
    if (auto comp = m_compositor.lock()) {
        comp->disableDsr();
    }
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

    // [FIX] Drain GL errors from pipeline compilation (shader compile/link,
    // FBO creation, texture allocation).  These are the #1 source of the stale
    // 0x502 that poisons SurfaceTexture.updateTexImage() on the first frame.
    {
        GLenum compileErr = glGetError();
        if (compileErr != GL_NO_ERROR) {
            LOGW("rebuildGraph: draining GL error(s) after pipeline compile: 0x%x", compileErr);
            int drainCount = 1;
            while ((compileErr = glGetError()) != GL_NO_ERROR && drainCount < 16) {
                LOGW("  additional compile GL error: 0x%x", compileErr);
                drainCount++;
            }
        }
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

// ============================================================================
// P1-6: GL Context Lost / Restored recovery
// ============================================================================

void FilterEngine::onContextLost() {
    LOGE("FilterEngine::onContextLost — all GPU handles are invalid, releasing CPU-side state.");
    // 强制清理 thread_local 状态机缓存，避免与纯净的新上下文状态脱节
    GLStateManager::getInstance().invalidateCache();
    // All GL objects (textures, FBOs, programs) are now dangling.
    // Release CPU bookkeeping so subsequent calls don't try to use them.
    if (m_graph) {
        m_graph->release();
        m_graph = nullptr;
    }
    m_cameraNode = nullptr;
    m_outputNode = nullptr;
    m_pendingFrames.clear();
    m_pendingFrameIds.clear();
    m_renderDevice = nullptr;
    m_frameBufferPool.clear();
    m_metricsCollector.clear();
    m_isGraphDirty = true;
    m_initialized = false;
    m_lastMode = m_currentMode;
    m_currentMode = PipelineMode::UNDEFINED;
    m_timeline = nullptr;
    m_compositor.reset();
    m_threadCheck.unbind();
    LOGW("FilterEngine::onContextLost — engine reset to UNINITIALIZED. Call initialize() after new context is current.");
}

Result FilterEngine::onContextRestored() {
    LOGI("FilterEngine::onContextRestored — re-initializing engine on new GL context.");
    // Re-run full initialization on the newly created GL context.
    // The caller is responsible for making the new context current on this thread first.
    Result res = initialize();
    if (!res.isOk()) {
        return res;
    }

    if (m_lastMode == PipelineMode::CAMERA) {
        LOGI("onContextRestored: Rebuilding camera pipeline");
        return buildCameraPipeline();
    } else if (m_lastMode == PipelineMode::TIMELINE) {
        LOGI("onContextRestored: Rebuilding timeline pipeline");
        if (auto compositor = m_compositor.lock()) {
            return buildTimelinePipeline(m_timeline, compositor);
        }
    }
    return Result::ok();
}

// ---------------------------------------------------------------------------
// onTrimMemory — Android ComponentCallbacks2 五档内存压力响应
//
// 策略（从重到轻）：
//   >= 80  TRIM_MEMORY_COMPLETE   : 清空所有 FBO，VRAM 上限  32 MB
//   >= 40  TRIM_MEMORY_BACKGROUND : 清空所有 FBO，VRAM 上限  64 MB
//   >= 20  TRIM_MEMORY_UI_HIDDEN  : 同上（UI 隐藏即强制清理）
//   >= 15  RUNNING_CRITICAL       : 触发 LRU 驱逐，VRAM 上限  64 MB
//   >= 10  RUNNING_LOW            : 收紧预算至 128 MB
//   >=  5  RUNNING_MODERATE       : 收紧预算至 192 MB
// ---------------------------------------------------------------------------
void FilterEngine::onTrimMemory(int level) {
    // ComponentCallbacks2 常量（与 Android API 对齐，不引入 JNI 依赖）
    constexpr int TRIM_RUNNING_MODERATE  =  5;
    constexpr int TRIM_RUNNING_LOW       = 10;
    constexpr int TRIM_RUNNING_CRITICAL  = 15;
    constexpr int TRIM_UI_HIDDEN         = 20;
    constexpr int TRIM_BACKGROUND        = 40;
    constexpr int TRIM_COMPLETE          = 80;

    constexpr size_t MB = 1024ULL * 1024ULL;

    if (level >= TRIM_UI_HIDDEN) {
        // 应用进入后台或内存极端紧张 → 清空全部缓存 FBO
        size_t budget = (level >= TRIM_COMPLETE)  ? 32  * MB :
                        (level >= TRIM_BACKGROUND) ? 64  * MB :
                                                      64  * MB; // UI_HIDDEN
        m_frameBufferPool.setVramBudget(budget);
        m_frameBufferPool.clear();
        LOGI("onTrimMemory(%d): FBO pool cleared, VRAM budget → %zu MB", level, budget / MB);
    } else if (level >= TRIM_RUNNING_CRITICAL) {
        m_frameBufferPool.setVramBudget(64 * MB);
        LOGI("onTrimMemory(%d): VRAM budget → 64 MB (LRU evict on next alloc)", level);
    } else if (level >= TRIM_RUNNING_LOW) {
        m_frameBufferPool.setVramBudget(128 * MB);
        LOGI("onTrimMemory(%d): VRAM budget → 128 MB", level);
    } else if (level >= TRIM_RUNNING_MODERATE) {
        m_frameBufferPool.setVramBudget(192 * MB);
        LOGI("onTrimMemory(%d): VRAM budget → 192 MB", level);
    }
}

void FilterEngine::markFrameRendered(uint32_t textureId) {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingFrames.erase(textureId);
    auto it = std::find(m_pendingFrameIds.begin(), m_pendingFrameIds.end(), textureId);
    if (it != m_pendingFrameIds.end()) {
        m_pendingFrameIds.erase(it);
    }
}

} // namespace video
} // namespace sdk
