#pragma once
#include "Timeline.h"
#include "Transition.h"
#include "SubtitleClip.h"
#include "ITextRasterizer.h"

#include "../GLTypes.h"
#include "../FrameBuffer.h"
#include "../PerformanceMetrics.h"
#include "../rhi/IRenderDevice.h"
#include "../rhi/IShaderProgram.h"
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <string>
#include <deque>
#include <algorithm>
#include <cmath>

namespace sdk {
namespace video {
class FilterEngine;
namespace timeline {

class IDecoderPool {
public:
    virtual ~IDecoderPool() = default;
    virtual ResultPayload<Texture> getFrame(const std::string& clipId, int64_t localTimeNs) = 0;
};

// ---------------------------------------------------------------------------
// DSR: Dynamic Resolution Scaling configuration
// ---------------------------------------------------------------------------
struct DsrConfig {
    float targetFps      = 30.0f;  // 目标帧率（Hz），超出则降低 scale
    float minScaleFactor = 0.5f;   // 最低分辨率倍率（50%）
    float maxScaleFactor = 1.0f;   // 最高分辨率倍率（100%，即原始）
    int   sampleWindow   = 6;      // 平均帧时间采样窗口大小（帧数）
};

class Compositor {
public:
    Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine);
    ~Compositor();

    void setDecoderPool(std::shared_ptr<IDecoderPool> decoderPool) {
        m_decoderPool = decoderPool;
    }

    void setTextRasterizer(std::shared_ptr<ITextRasterizer> rasterizer) {
        m_textRasterizer = rasterizer;
    }

    // 启用动态分辨率缩放。调用后立即生效，dsrScale 从 1.0 开始自适应调整。
    void setDsrConfig(const DsrConfig& cfg) {
        m_dsrConfig  = cfg;
        m_dsrEnabled = true;
        m_dsrScale   = cfg.maxScaleFactor;
        m_dsrFrameTimes.clear();
    }
    // 禁用 DSR，恢复全分辨率渲染
    void disableDsr() { m_dsrEnabled = false; m_dsrScale = 1.0f; }
    // 当前实际渲染倍率 [minScale, maxScale]
    float getDsrScale() const { return m_dsrScale; }

    std::shared_ptr<FilterEngine> getFilterEngine() const;
    void setFilterEngine(std::shared_ptr<FilterEngine> engine);

    Result renderFrameAtTime(int64_t timelineNs, FrameBufferPtr outputFb);

    PerformanceMetrics getMetrics() const { return m_metricsCollector.getMetrics(); }

private:
    mutable MetricsCollector m_metricsCollector;
    std::shared_ptr<Timeline> m_timeline;
    std::shared_ptr<FilterEngine> m_filterEngine;
    std::shared_ptr<IDecoderPool> m_decoderPool;
    std::vector<ClipPtr> m_activeClips;

    // RHI shader programs（替代裸 GLuint）
    std::shared_ptr<rhi::IShaderProgram> m_copyProgram;
    std::shared_ptr<rhi::IShaderProgram> m_blendProgram;
    // 转场 shader 懒编译缓存：TransitionRegistry key -> IShaderProgram
    std::unordered_map<std::string, std::shared_ptr<rhi::IShaderProgram>> m_transitionPrograms;
    // 叠加层（字幕/贴纸）专用 alpha-blend shader
    std::shared_ptr<rhi::IShaderProgram> m_overlayProgram;
    // 平台层注入的文字光栅化器（可为 nullptr，则跳过字幕渲染）
    std::shared_ptr<ITextRasterizer> m_textRasterizer;
    // 当前帧叠加层 clip 列表
    std::vector<ClipPtr> m_overlayClips;

    // ---------------------------------------------------------------------------
    // DSR (Dynamic Resolution Scaling)
    // ---------------------------------------------------------------------------
    DsrConfig         m_dsrConfig;
    float             m_dsrScale     = 1.0f;  // 当前渲染倍率，[minScale, maxScale]
    bool              m_dsrEnabled   = true;
    std::deque<float> m_dsrFrameTimes;         // 近 N 帧帧时间（ms）

    void updateDsrScale(float frameTimeMs);

    // ---------------------------------------------------------------------------
    // GPU timestamp query (GL_EXT_disjoint_timer_query on Android GLES)
    // ---------------------------------------------------------------------------
    static constexpr int GPU_QUERY_BUFFERS = 2;
    unsigned int m_gpuQueries[GPU_QUERY_BUFFERS] = {0, 0};   // GLuint
    bool         m_gpuQueriesPending[GPU_QUERY_BUFFERS] = {false, false};
    int          m_gpuQueryIndex   = 0;
    bool         m_gpuTimerInited  = false;
    bool         m_gpuTimerOk      = false;  // extension available

    bool  initGpuTimer();
    float retrieveGpuTimeMs(int index);  // returns -1 if not ready / unavailable

    // ---------------------------------------------------------------------------
    // 通用顶点着色器源（所有四边形程序共用）
    // ---------------------------------------------------------------------------
    static const char* s_quadVertSrc;

    Result initPrograms();
    Result initCopyProgram();
    Result initBlendProgram();
    Result initOverlayProgram();

    // 从 TransitionRegistry 懒编译并缓存转场 shader
    // 返回对应 IShaderProgram，失败返回 nullptr
    std::shared_ptr<rhi::IShaderProgram> getOrCompileTransition(const std::string& name);

    // 执行全屏四边形 draw call（在当前绑定的 FBO 上）
    static void drawQuad();

    Result copyTexture(const Texture& src, FrameBufferPtr target, float opacity = 1.0f);

    ResultPayload<Texture> blendTextures(const Texture& bg, const Texture& fg,
                                          float opacity, FrameBufferPtr target);
    ResultPayload<Texture> transitionTextures(const Texture& bg, const Texture& fg,
                                               const std::string& transitionName,
                                               float progress, FrameBufferPtr target);
};

} // namespace timeline
} // namespace video
} // namespace sdk
