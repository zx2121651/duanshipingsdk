#pragma once
/**
 * DepthEstimator.h
 *
 * 单目深度估计接口（P3 补齐）。
 *
 * 架构：
 *   - 定义统一的深度估计接口，后端可插拔：
 *     * TFLite 后端（MiDaS / Depth-Anything / monodepth2）
 *     * 梯度近似后端（CPU 快速版，用于无模型降级）
 *   - 输出 DepthMap（归一化深度图，float32，[0,1]，0=近，1=远）
 *   - 提供背景虚化（Bokeh）遮罩生成工具函数
 *   - 无硬依赖，TFLite 后端通过 IDepthBackend 注入
 *
 * 用法：
 *   DepthEstimator estimator;
 *   // 可选：注入高质量 TFLite 后端
 *   estimator.setBackend(std::make_shared<MiDaSBackend>("midas.tflite"));
 *   auto depthMap = estimator.estimate(rgba, width, height);
 *   // 生成背景虚化遮罩
 *   auto mask = estimator.buildBokehMask(depthMap, focalDepth=0.3f, radius=0.15f);
 */

#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <cmath>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// 深度图结构
// ---------------------------------------------------------------------------
struct DepthMap {
    std::vector<float> data;  ///< 归一化深度 [0,1]，0=近，1=远
    int width  = 0;
    int height = 0;
    bool valid = false;

    float at(int x, int y) const {
        if (x<0||x>=width||y<0||y>=height) return 0.f;
        return data[y*width+x];
    }
};

// ---------------------------------------------------------------------------
// 可插拔后端接口
// ---------------------------------------------------------------------------
class IDepthBackend {
public:
    virtual ~IDepthBackend() = default;
    /**
     * 估计深度图。
     * @param rgba   输入 RGBA 图像
     * @param w/h    输入尺寸
     * @return       DepthMap（valid=false 表示失败）
     */
    virtual DepthMap estimate(const uint8_t* rgba, int w, int h) = 0;
    virtual const char* name() const = 0;
};

// ---------------------------------------------------------------------------
// DepthEstimator
// ---------------------------------------------------------------------------
class DepthEstimator {
public:
    DepthEstimator();
    ~DepthEstimator() = default;

    // ── 后端 ──────────────────────────────────────────────────────────────

    /** 注入 TFLite 或其他高质量后端。 */
    void setBackend(std::shared_ptr<IDepthBackend> backend);

    /** 是否已注入高质量后端（否则使用内置梯度近似）。 */
    bool hasBackend() const { return m_backend != nullptr; }

    // ── 深度估计 ──────────────────────────────────────────────────────────

    /**
     * 估计深度图（自动选择最佳后端）。
     * @param rgba       RGBA 图像
     * @param w/h        图像尺寸
     * @param outW/outH  输出深度图尺寸（0=与输入相同）
     */
    DepthMap estimate(const uint8_t* rgba, int w, int h,
                      int outW = 0, int outH = 0);

    // ── 应用工具 ──────────────────────────────────────────────────────────

    /**
     * 生成背景虚化遮罩（前景=1.0，背景=0.0，过渡区平滑）。
     * @param depthMap    深度图（0=近，1=远）
     * @param focalDepth  焦平面深度 [0,1]
     * @param dofRadius   景深范围（在焦平面上下各 radius 为清晰区）
     * @return            float 遮罩图，与 depthMap 同尺寸
     */
    static std::vector<float> buildBokehMask(const DepthMap& depthMap,
                                              float focalDepth = 0.3f,
                                              float dofRadius  = 0.15f);

    /**
     * 将深度图调整到指定尺寸（双线性插值）。
     */
    static DepthMap resize(const DepthMap& src, int dstW, int dstH);

    /**
     * 深度图转伪彩色 RGBA（用于调试可视化）。
     * 0(近)=蓝，0.5=绿，1(远)=红
     */
    static std::vector<uint8_t> toColormap(const DepthMap& depthMap);

    // ── 统计 ──────────────────────────────────────────────────────────────

    float minDepth(const DepthMap& dm) const;
    float maxDepth(const DepthMap& dm) const;
    float meanDepth(const DepthMap& dm) const;

private:
    std::shared_ptr<IDepthBackend> m_backend;

    // 内置：梯度近似深度（CPU，用于无模型降级）
    DepthMap estimateGradient(const uint8_t* rgba, int w, int h,
                               int outW, int outH) const;

    static float lerpDepth(const DepthMap& src, float x, float y);
};

} // namespace ai
} // namespace video
} // namespace sdk
