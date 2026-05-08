#pragma once
/**
 * SmartCropController.h
 *
 * 智能裁剪控制器（P3 补齐）。
 *
 * 功能：
 *   - 根据 AI 检测结果（人脸 / 人体 / 物体）自动计算裁剪区域
 *   - 保持指定宽高比（9:16/16:9/1:1/4:3等）
 *   - 关键帧平滑过渡（防止裁剪区域抖动）
 *   - 支持"规则三分法"构图（主体在黄金比例位置）
 *   - 输出归一化裁剪矩形 [0,1]，供 Compositor/TimelineExporter 使用
 *
 * 集成方式：
 *   1. 每帧检测后调用 updateDetection() 推入主体区域
 *   2. 调用 compute() 获取当前帧建议的裁剪矩形
 *   3. 将裁剪矩形注入到 Clip 的 transform 或 shader uniform
 */

#include <vector>
#include <functional>
#include <cstdint>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
struct CropRect {
    float x = 0.f, y = 0.f;   ///< 左上角（归一化）
    float w = 1.f, h = 1.f;   ///< 宽高（归一化）
    bool  valid() const { return w > 0.f && h > 0.f; }
};

struct SubjectRegion {
    CropRect rect;
    float    confidence = 1.f;
    enum class Type { FACE, BODY, OBJECT } type = Type::FACE;
};

// ---------------------------------------------------------------------------
class SmartCropController {
public:
    enum class AspectRatio {
        RATIO_9_16  = 0,  ///< 竖屏 9:16
        RATIO_16_9  = 1,  ///< 横屏 16:9
        RATIO_1_1   = 2,  ///< 正方形 1:1
        RATIO_4_3   = 3,  ///< 4:3
        RATIO_3_4   = 4,  ///< 3:4 (竖屏)
        CUSTOM      = 5,
    };

    SmartCropController();
    ~SmartCropController() = default;

    // ── 配置 ──────────────────────────────────────────────────────────────

    void setAspectRatio(AspectRatio ratio);
    void setCustomAspectRatio(float w, float h);

    /** 平滑系数 [0,1]：0=立即跟随，1=不跟随，默认 0.85（强平滑）。 */
    void setSmoothFactor(float f) { m_smoothFactor = f; }

    /** 是否启用三分法构图（主体偏向 1/3 分割线）。 */
    void setRuleOfThirds(bool enable) { m_ruleOfThirds = enable; }

    /** 主体类型优先级（高优先级主体优先决定裁剪）。 */
    void setPreferredSubjectType(SubjectRegion::Type t) { m_preferredType = t; }

    /** 边缘保护边距（归一化），防止主体被裁切太边缘，默认 0.05。 */
    void setEdgePadding(float p) { m_edgePadding = p; }

    // ── 检测数据输入 ──────────────────────────────────────────────────────

    /**
     * 每帧调用：推入当前帧检测到的主体区域列表。
     * @param subjects  检测结果（可为空，表示无主体）
     */
    void updateDetection(const std::vector<SubjectRegion>& subjects);

    // ── 计算裁剪 ──────────────────────────────────────────────────────────

    /**
     * 计算当前帧建议裁剪矩形（归一化，基于平滑后的主体中心）。
     * @param frameW  原始帧宽（像素，用于保持像素比例）
     * @param frameH  原始帧高
     * @return        建议裁剪矩形（归一化）
     */
    CropRect compute(int frameW, int frameH);

    /** 重置平滑状态（切换镜头时调用）。 */
    void reset();

    /** 获取最后一次 compute() 的结果（不重新计算）。 */
    CropRect lastCrop() const { return m_currentCrop; }

    /** 当前平滑后的主体中心（归一化）。 */
    float smoothedCenterX() const { return m_smoothCX; }
    float smoothedCenterY() const { return m_smoothCY; }

private:
    AspectRatio m_ratio     = AspectRatio::RATIO_9_16;
    float  m_ratioW         = 9.f;
    float  m_ratioH         = 16.f;
    float  m_smoothFactor   = 0.85f;
    bool   m_ruleOfThirds   = true;
    float  m_edgePadding    = 0.05f;
    SubjectRegion::Type m_preferredType = SubjectRegion::Type::FACE;

    // Smoothed state
    float  m_smoothCX       = 0.5f;
    float  m_smoothCY       = 0.4f;
    bool   m_initialized    = false;
    CropRect m_currentCrop;

    // Latest detection
    std::vector<SubjectRegion> m_lastSubjects;

    CropRect buildCropRect(float cx, float cy, float frameAspect) const;
    static void getRatioWH(AspectRatio r, float& w, float& h);
};

} // namespace timeline
} // namespace video
} // namespace sdk
