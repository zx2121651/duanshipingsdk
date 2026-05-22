#pragma once
/**
 * HistogramAnalyzer.h
 *
 * 亮度/RGB/HSV 直方图分析器（P3 补齐）。
 *
 * 功能：
 *   - CPU 端计算 RGBA 帧的 R/G/B/Luma 直方图（256 bins）
 *   - 提供均值/中位数/标准差/百分位数统计
 *   - 过曝/欠曝检测（超出高光/阴影阈值的像素比例）
 *   - 波形图数据生成（按列统计亮度分布，用于视频监看）
 *   - 纯 CPU，无 GL 依赖，可在后台线程运行
 *
 * 用法：
 *   HistogramAnalyzer analyzer;
 *   analyzer.analyze(rgba, width, height);
 *   auto& hist = analyzer.getLumaHistogram();
 *   float mean = analyzer.lumaMean();
 *   bool overExp = analyzer.isOverexposed(0.05f);  // >5% 像素过曝
 */

#include <array>
#include <vector>
#include <cstdint>

namespace sdk {
namespace video {
namespace timeline {

struct ChannelStats {
    float mean     = 0.f;
    float stddev   = 0.f;
    float median   = 0.f;
    float p5       = 0.f;   ///< 5th percentile
    float p95      = 0.f;   ///< 95th percentile
    float min      = 0.f;
    float max      = 0.f;
};

class HistogramAnalyzer {
public:
    static constexpr int kBins = 256;

    HistogramAnalyzer() = default;
    ~HistogramAnalyzer() = default;

    // ── 分析 ──────────────────────────────────────────────────────────────

    /**
     * 分析一帧 RGBA 图像。
     * @param rgba    像素数据（行主序，每像素 4 字节 RGBA）
     * @param w/h     图像宽高
     * @param stride  行步长（字节，0=w*4）
     */
    void analyze(const uint8_t* rgba, int w, int h, int stride = 0);

    // ── 直方图数据 ─────────────────────────────────────────────────────────

    const std::array<uint32_t, kBins>& getLumaHistogram()  const { return m_histLuma; }
    const std::array<uint32_t, kBins>& getRedHistogram()   const { return m_histR; }
    const std::array<uint32_t, kBins>& getGreenHistogram() const { return m_histG; }
    const std::array<uint32_t, kBins>& getBlueHistogram()  const { return m_histB; }

    /** 归一化直方图（峰值 = 1.0），用于 UI 绘制。 */
    std::array<float, kBins> getNormalizedHistogram(
        const std::array<uint32_t, kBins>& hist) const;

    // ── 统计值 ─────────────────────────────────────────────────────────────

    ChannelStats lumaStats()  const { return m_statsLuma; }
    ChannelStats redStats()   const { return m_statsR; }
    ChannelStats greenStats() const { return m_statsG; }
    ChannelStats blueStats()  const { return m_statsB; }

    float lumaMean()   const { return m_statsLuma.mean; }
    float lumaStddev() const { return m_statsLuma.stddev; }
    float lumaMedian() const { return m_statsLuma.median; }

    // ── 过曝/欠曝检测 ──────────────────────────────────────────────────────

    /**
     * 是否过曝。
     * @param threshold  过曝像素占比阈值（如 0.05 = 5%）
     * @param brightLevel 亮度阈值（0-255，高于此值视为过曝，默认 245）
     */
    bool isOverexposed(float threshold = 0.05f, int brightLevel = 245) const;

    /**
     * 是否欠曝。
     * @param threshold  欠曝像素占比阈值
     * @param darkLevel  亮度阈值（低于此值视为欠曝，默认 15）
     */
    bool isUnderexposed(float threshold = 0.05f, int darkLevel = 15) const;

    /** 过曝像素比例 [0,1]。 */
    float overexposedRatio(int brightLevel = 245) const;

    /** 欠曝像素比例 [0,1]。 */
    float underexposedRatio(int darkLevel = 15) const;

    // ── 波形图（Waveform）──────────────────────────────────────────────────

    /**
     * 生成亮度波形图数据（按列统计亮度分布）。
     * @param numColumns  波形列数（通常与输出宽度相同）
     * @return            [numColumns][kBins] 的密度图，值为 [0,1]
     */
    std::vector<std::array<float, kBins>> buildWaveform(int numColumns) const;

    // ── 元数据 ─────────────────────────────────────────────────────────────

    int frameWidth()  const { return m_w; }
    int frameHeight() const { return m_h; }
    int totalPixels() const { return m_totalPixels; }
    bool isValid()    const { return m_totalPixels > 0; }

private:
    int m_w = 0, m_h = 0, m_totalPixels = 0;

    std::array<uint32_t, kBins> m_histLuma{}, m_histR{}, m_histG{}, m_histB{};

    ChannelStats m_statsLuma, m_statsR, m_statsG, m_statsB;

    // Store raw luma values for waveform generation
    std::vector<uint8_t> m_lumaData;

    static ChannelStats computeStats(const std::array<uint32_t, kBins>& hist,
                                      int totalPixels);
};

} // namespace timeline
} // namespace video
} // namespace sdk
