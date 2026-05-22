#pragma once
/**
 * ObjectTracker.h
 *
 * 轻量物体跟踪器（无 OpenCV 依赖）。
 *
 * 算法后端：
 *   MEAN_SHIFT   — 颜色直方图均值漂移，适用慢速运动
 *   TEMPLATE     — 归一化互相关模板匹配，适用纹理丰富目标
 *   HYBRID       — 先 TEMPLATE 定位，MEAN_SHIFT 精细化
 *
 * 用法（宠物贴纸 / 物体 AR 锚定）：
 *   ObjectTracker tracker;
 *   // 第一帧指定 ROI 初始化
 *   tracker.init(frame.rgba, frame.w, frame.h, {x, y, w, h});
 *   // 后续帧跟踪
 *   auto result = tracker.update(nextFrame.rgba, nextFrame.w, nextFrame.h);
 *   if (result.found) {
 *       drawStickerAt(result.rect.x, result.rect.y, result.rect.w, result.rect.h);
 *   }
 */

#include <cstdint>
#include <vector>
#include <functional>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
struct TrackRect {
    float x = 0.f, y = 0.f;   ///< 左上角（归一化 [0,1]）
    float w = 0.f, h = 0.f;   ///< 宽高（归一化）
    bool  valid() const { return w > 0.f && h > 0.f; }
};

struct TrackResult {
    bool      found    = false;
    TrackRect rect;
    float     confidence = 0.f;   ///< [0,1]
    int       frameIdx   = 0;
};

// ---------------------------------------------------------------------------
class ObjectTracker {
public:
    enum class Backend { MEAN_SHIFT, TEMPLATE, HYBRID };

    ObjectTracker();
    ~ObjectTracker() = default;

    // ── 配置 ──────────────────────────────────────────────────────────────

    void setBackend(Backend b) { m_backend = b; }

    /** 模板匹配搜索半径（像素，内部降采样后），默认 40。 */
    void setSearchRadius(int r) { m_searchRadius = r; }

    /** 最小置信度阈值，低于该值认为跟踪丢失，默认 0.35。 */
    void setConfidenceThreshold(float t) { m_confThreshold = t; }

    /** 目标移动过快时自动重新初始化（依赖检测器注入），默认关闭。 */
    void setAutoReinit(bool v) { m_autoReinit = v; }

    /**
     * 注入外部检测器（用于跟踪丢失后重新锚定）。
     * fn(rgba, w, h) → 返回 ROI，未检测到则返回 invalid TrackRect
     */
    using DetectorFn = std::function<TrackRect(const uint8_t*, int, int)>;
    void setDetector(DetectorFn fn) { m_detector = std::move(fn); }

    // ── 跟踪 ──────────────────────────────────────────────────────────────

    /**
     * 初始化跟踪器（第一帧 + 目标 ROI）。
     * @param rgba   RGBA 图像数据
     * @param w/h    图像尺寸
     * @param roi    归一化目标区域
     */
    bool init(const uint8_t* rgba, int w, int h, const TrackRect& roi);

    /**
     * 在新帧中跟踪目标。
     * @return       跟踪结果（found=false 表示丢失）
     */
    TrackResult update(const uint8_t* rgba, int w, int h);

    /** 重置跟踪器。 */
    void reset();

    bool isInitialized() const { return m_initialized; }
    TrackRect getLastRect() const { return m_currentRect; }

private:
    Backend m_backend        = Backend::HYBRID;
    int     m_searchRadius   = 40;
    float   m_confThreshold  = 0.35f;
    bool    m_autoReinit     = false;
    bool    m_initialized    = false;
    int     m_frameIdx       = 0;

    DetectorFn m_detector;

    // Internal dimensions (downsampled)
    static constexpr int kScale = 4; // 1/4 resolution for speed
    int m_dw = 0, m_dh = 0;

    TrackRect m_currentRect;

    // Template patch (grayscale, normalized 0–255)
    std::vector<float> m_tmpl;
    int m_tmplW = 0, m_tmplH = 0;

    // Color histogram (HSV, 16 bins per channel)
    static constexpr int kHistBins = 16;
    float m_histH[kHistBins]{}, m_histS[kHistBins]{};

    // ── Algorithm implementations ─────────────────────────────────────────

    // Downsampled grayscale buffer
    static void toGray(const uint8_t* rgba, int w, int h,
                       std::vector<float>& gray);
    static void downsample(const std::vector<float>& src, int sw, int sh,
                           std::vector<float>& dst, int dw, int dh);

    // Template matching: returns best offset (dx, dy in full-res pixels) + NCC score
    float templateMatch(const std::vector<float>& frame, int fw, int fh,
                        int searchX, int searchY, int searchW, int searchH,
                        float& outDx, float& outDy) const;

    // Mean shift on color histogram
    TrackResult meanShiftUpdate(const uint8_t* rgba, int w, int h);

    // Build HSV histogram from RGBA roi
    void buildHistogram(const uint8_t* rgba, int w, int h, const TrackRect& roi);

    // Pixel to histogram bin
    static int hueBin(float h) { return (int)(h / (360.f/kHistBins)) % kHistBins; }
    static int satBin(float s) { return std::min((int)(s * kHistBins), kHistBins-1); }

    // RGBA at (px, py) in full image
    static void rgbaAt(const uint8_t* rgba, int w, int h,
                       int px, int py, float& r, float& g, float& b);
    static void rgb2hsv(float r, float g, float b, float& h, float& s, float& v);

    // Convert normalized ROI ↔ pixel coords
    TrackRect toNorm(int px, int py, int pw, int ph, int W, int H) const;
    void fromNorm(const TrackRect& r, int W, int H,
                  int& px, int& py, int& pw, int& ph) const;
};

} // namespace ai
} // namespace video
} // namespace sdk
