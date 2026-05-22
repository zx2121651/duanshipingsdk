/**
 * HistogramAnalyzer.cpp
 */

#include "../../include/timeline/HistogramAnalyzer.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
void HistogramAnalyzer::analyze(const uint8_t* rgba, int w, int h, int stride) {
    m_w = w; m_h = h;
    m_totalPixels = w * h;
    if (m_totalPixels == 0 || !rgba) return;

    int rowBytes = (stride > 0) ? stride : w * 4;

    m_histLuma.fill(0); m_histR.fill(0); m_histG.fill(0); m_histB.fill(0);
    m_lumaData.resize(m_totalPixels);

    int idx = 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = rgba + y * rowBytes;
        for (int x = 0; x < w; ++x, ++idx) {
            uint8_t r = row[x*4+0], g = row[x*4+1], b = row[x*4+2];
            // ITU-R BT.601 luma
            uint8_t luma = (uint8_t)((77*r + 150*g + 29*b) >> 8);
            m_histLuma[luma]++;
            m_histR[r]++;
            m_histG[g]++;
            m_histB[b]++;
            m_lumaData[idx] = luma;
        }
    }

    m_statsLuma = computeStats(m_histLuma, m_totalPixels);
    m_statsR    = computeStats(m_histR,    m_totalPixels);
    m_statsG    = computeStats(m_histG,    m_totalPixels);
    m_statsB    = computeStats(m_histB,    m_totalPixels);
}

// ---------------------------------------------------------------------------
ChannelStats HistogramAnalyzer::computeStats(
    const std::array<uint32_t, kBins>& hist, int totalPixels)
{
    ChannelStats s;
    if (totalPixels == 0) return s;

    double sum = 0.0, sumSq = 0.0;
    int minBin = -1, maxBin = -1;
    for (int i = 0; i < kBins; ++i) {
        if (hist[i] == 0) continue;
        if (minBin < 0) minBin = i;
        maxBin = i;
        sum   += (double)i * hist[i];
        sumSq += (double)i * i * hist[i];
    }
    if (minBin < 0) return s;

    s.mean   = (float)(sum / totalPixels);
    s.stddev = (float)std::sqrt(sumSq/totalPixels - (sum/totalPixels)*(sum/totalPixels));
    s.min    = (float)minBin;
    s.max    = (float)maxBin;

    // Percentile computation via cumulative sum
    uint64_t cumul = 0;
    bool p5done=false, p50done=false, p95done=false;
    for (int i = 0; i < kBins; ++i) {
        cumul += hist[i];
        float pct = (float)cumul / totalPixels;
        if (!p5done  && pct >= 0.05f) { s.p5     = (float)i; p5done  = true; }
        if (!p50done && pct >= 0.50f) { s.median = (float)i; p50done = true; }
        if (!p95done && pct >= 0.95f) { s.p95    = (float)i; p95done = true; }
    }
    return s;
}

// ---------------------------------------------------------------------------
std::array<float, HistogramAnalyzer::kBins> HistogramAnalyzer::getNormalizedHistogram(
    const std::array<uint32_t, kBins>& hist) const
{
    std::array<float, kBins> out{};
    uint32_t peak = *std::max_element(hist.begin(), hist.end());
    if (peak == 0) return out;
    for (int i = 0; i < kBins; ++i) out[i] = (float)hist[i] / peak;
    return out;
}

// ---------------------------------------------------------------------------
float HistogramAnalyzer::overexposedRatio(int brightLevel) const {
    if (m_totalPixels == 0) return 0.f;
    uint64_t cnt = 0;
    for (int i = std::max(0, brightLevel); i < kBins; ++i) cnt += m_histLuma[i];
    return (float)cnt / m_totalPixels;
}

float HistogramAnalyzer::underexposedRatio(int darkLevel) const {
    if (m_totalPixels == 0) return 0.f;
    uint64_t cnt = 0;
    for (int i = 0; i <= std::min(kBins-1, darkLevel); ++i) cnt += m_histLuma[i];
    return (float)cnt / m_totalPixels;
}

bool HistogramAnalyzer::isOverexposed(float threshold, int brightLevel) const {
    return overexposedRatio(brightLevel) > threshold;
}
bool HistogramAnalyzer::isUnderexposed(float threshold, int darkLevel) const {
    return underexposedRatio(darkLevel) > threshold;
}

// ---------------------------------------------------------------------------
std::vector<std::array<float, HistogramAnalyzer::kBins>>
HistogramAnalyzer::buildWaveform(int numColumns) const
{
    std::vector<std::array<float, kBins>> waveform(numColumns);
    for (auto& col : waveform) col.fill(0.f);
    if (m_lumaData.empty() || numColumns <= 0) return waveform;

    // Count per column
    std::vector<uint32_t> colCounts(numColumns, 0);
    for (int y = 0; y < m_h; ++y) {
        for (int x = 0; x < m_w; ++x) {
            int col = x * numColumns / m_w;
            uint8_t luma = m_lumaData[y * m_w + x];
            waveform[col][luma]++;
            colCounts[col]++;
        }
    }
    // Normalize each column
    for (int c = 0; c < numColumns; ++c) {
        if (colCounts[c] == 0) continue;
        uint32_t peak = *std::max_element(waveform[c].begin(), waveform[c].end());
        if (peak == 0) continue;
        for (float& v : waveform[c]) v /= peak;
    }
    return waveform;
}

} // namespace timeline
} // namespace video
} // namespace sdk
