/**
 * SmartCoverSelector.cpp
 *
 * 无 GPU、无外部依赖的纯 CPU 封面候选帧评分器。
 */

#include "../../include/timeline/SmartCoverSelector.h"

#define LOG_TAG "SmartCoverSelector"
#include "../../include/Log.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>

namespace sdk {
namespace video {
namespace timeline {

SmartCoverSelector::SmartCoverSelector() = default;

// ---------------------------------------------------------------------------
// downsample — bilinear RGBA 缩小（用于快速分析）
// ---------------------------------------------------------------------------
void SmartCoverSelector::downsample(const uint8_t* src, int sw, int sh,
                                     uint8_t* dst, int dw, int dh)
{
    for (int dy = 0; dy < dh; ++dy) {
        for (int dx = 0; dx < dw; ++dx) {
            int sx = dx * sw / dw;
            int sy = dy * sh / dh;
            int idx = (sy * sw + sx) * 4;
            int out  = (dy * dw + dx) * 4;
            dst[out+0] = src[idx+0];
            dst[out+1] = src[idx+1];
            dst[out+2] = src[idx+2];
            dst[out+3] = src[idx+3];
        }
    }
}

// ---------------------------------------------------------------------------
// computeBrightness — 均值亮度，惩罚极端值
// ---------------------------------------------------------------------------
float SmartCoverSelector::computeBrightness(const uint8_t* rgba, int w, int h)
{
    int64_t sum = 0;
    int     n   = w * h;
    for (int i = 0; i < n; ++i) {
        const uint8_t* p = rgba + i * 4;
        sum += (int)(0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]);
    }
    float mean = sum / (float)n;
    // Penalize very dark (<40) or very bright (>220)
    float penalty = std::fabs(mean - 128.f) / 128.f;
    return 1.0f - penalty * 0.6f;
}

// ---------------------------------------------------------------------------
// computeSharpness — Laplacian variance (fast 3×3 approx)
// ---------------------------------------------------------------------------
float SmartCoverSelector::computeSharpness(const uint8_t* rgba, int w, int h)
{
    if (w < 3 || h < 3) return 0.f;
    double sumSq = 0.0, sum = 0.0;
    int    count = 0;
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            // Luma of center + neighbors
            auto luma = [&](int px, int py) -> float {
                const uint8_t* p = rgba + (py * w + px) * 4;
                return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
            };
            float lap = -4.f * luma(x, y)
                        + luma(x-1, y) + luma(x+1, y)
                        + luma(x, y-1) + luma(x, y+1);
            sumSq += lap * lap;
            sum   += lap;
            ++count;
        }
    }
    if (count == 0) return 0.f;
    double var = sumSq / count - (sum / count) * (sum / count);
    // Normalize: typical sharp frame ≈ variance 500; clamp to [0,1]
    return (float)std::min(var / 1000.0, 1.0);
}

// ---------------------------------------------------------------------------
// computeMotion — mean absolute inter-frame diff (luma)
// ---------------------------------------------------------------------------
float SmartCoverSelector::computeMotion(const uint8_t* cur, const uint8_t* prev,
                                         int w, int h)
{
    int64_t sumDiff = 0;
    int     n       = w * h;
    for (int i = 0; i < n; ++i) {
        const uint8_t* c = cur  + i * 4;
        const uint8_t* p = prev + i * 4;
        float lc = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
        float lp = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
        sumDiff += (int64_t)std::fabs(lc - lp);
    }
    return (float)(sumDiff / (n * 255.0));  // [0,1]
}

// ---------------------------------------------------------------------------
// submitFrame
// ---------------------------------------------------------------------------
void SmartCoverSelector::submitFrame(const uint8_t* rgba, int width, int height,
                                      int64_t timeNs)
{
    // Downsample for speed (max 128×128)
    constexpr int kAnalysisW = 128;
    constexpr int kAnalysisH = 128;
    std::vector<uint8_t> small(kAnalysisW * kAnalysisH * 4);
    downsample(rgba, width, height, small.data(), kAnalysisW, kAnalysisH);

    FrameRecord rec;
    rec.timeNs = timeNs;
    rec.w = kAnalysisW; rec.h = kAnalysisH;

    if (m_strategy & BRIGHTNESS)
        rec.brightness = computeBrightness(small.data(), kAnalysisW, kAnalysisH);

    if (m_strategy & SHARPNESS)
        rec.sharpness = computeSharpness(small.data(), kAnalysisW, kAnalysisH);

    if ((m_strategy & MOTION_LOW) && !m_prevPixels.empty() &&
        m_prevW == kAnalysisW && m_prevH == kAnalysisH) {
        rec.motion = computeMotion(small.data(), m_prevPixels.data(),
                                   kAnalysisW, kAnalysisH);
    }

    if (m_extractThumbs) {
        rec.pixels.resize((size_t)(m_thumbW * m_thumbH * 4));
        downsample(rgba, width, height, rec.pixels.data(), m_thumbW, m_thumbH);
        rec.w = m_thumbW; rec.h = m_thumbH;
    }

    m_prevPixels = small;
    m_prevW = kAnalysisW; m_prevH = kAnalysisH;

    m_frames.push_back(std::move(rec));
}

// ---------------------------------------------------------------------------
// selectCandidates
// ---------------------------------------------------------------------------
std::vector<CoverCandidate> SmartCoverSelector::selectCandidates() {
    if (m_frames.empty()) return {};

    // Compute combined score for each frame
    std::vector<std::pair<float, size_t>> scored; // (score, index)
    scored.reserve(m_frames.size());

    for (size_t i = 0; i < m_frames.size(); ++i) {
        const auto& f = m_frames[i];
        float score = 0.f;
        float wSum  = 0.f;

        if (m_strategy & BRIGHTNESS) { score += f.brightness * 1.0f; wSum += 1.f; }
        if (m_strategy & SHARPNESS)  { score += f.sharpness  * 1.5f; wSum += 1.5f; }
        if (m_strategy & MOTION_LOW) { score += (1.f - f.motion) * 1.2f; wSum += 1.2f; }
        if (m_strategy & FACE_PRESENT) {
            score += (f.hasFace ? 1.f : 0.f) * 2.0f; wSum += 2.f;
        }
        if (wSum > 0.f) score /= wSum;
        else            score  = 0.5f;

        scored.push_back({score, i});
    }

    // Sort descending by score
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b){ return a.first > b.first; });

    std::vector<CoverCandidate> result;
    int take = std::min(m_candidateCount, (int)scored.size());
    result.reserve(take);
    for (int k = 0; k < take; ++k) {
        size_t idx = scored[k].second;
        const auto& f = m_frames[idx];
        CoverCandidate c;
        c.timeNs     = f.timeNs;
        c.score      = scored[k].first;
        c.brightness = f.brightness;
        c.sharpness  = f.sharpness;
        c.motionBlur = f.motion;
        c.hasFace    = f.hasFace;
        if (m_extractThumbs && !f.pixels.empty()) {
            c.thumbnail   = f.pixels;
            c.thumbWidth  = m_thumbW;
            c.thumbHeight = m_thumbH;
        }
        result.push_back(std::move(c));
    }
    return result;
}

CoverCandidate SmartCoverSelector::selectBest() {
    auto cands = selectCandidates();
    if (cands.empty()) { CoverCandidate c; return c; }
    return cands.front();
}

} // namespace timeline
} // namespace video
} // namespace sdk
