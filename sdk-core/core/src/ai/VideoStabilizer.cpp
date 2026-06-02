/**
 * VideoStabilizer.cpp
 *
 * 无 GPU、无 OpenCV 依赖的轻量防抖实现。
 *
 * 光流后端：稀疏块匹配（5×5 patch, 9×9 搜索窗口），足以稳定手机拍摄抖动。
 * 可通过平台层替换为 OpenCV calcOpticalFlowPyrLK 获得更高精度。
 * 平滑算法：高斯核轨迹平滑（Gaussian-weighted moving average）。
 */

#include "../../include/ai/VideoStabilizer.h"

#define LOG_TAG "VideoStabilizer"
#include "../../include/Log.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
VideoStabilizer::VideoStabilizer() = default;

void VideoStabilizer::reset() {
    m_rawMotions.clear();
    m_smoothedMotions.clear();
    m_prevGray.clear();
    m_prevW = m_prevH = 0;
}

// ---------------------------------------------------------------------------
// rgbaToGray
// ---------------------------------------------------------------------------
void VideoStabilizer::rgbaToGray(const uint8_t* rgba, int w, int h, uint8_t* gray) {
    int n = w * h;
    for (int i = 0; i < n; ++i) {
        const uint8_t* p = rgba + i * 4;
        gray[i] = (uint8_t)(0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]);
    }
}

void VideoStabilizer::downsample(const uint8_t* src, int sw, int sh,
                                  uint8_t* dst, int dw, int dh)
{
    for (int dy = 0; dy < dh; ++dy)
        for (int dx = 0; dx < dw; ++dx)
            dst[dy * dw + dx] = src[(dy * sh / dh) * sw + (dx * sw / dw)];
}

// ---------------------------------------------------------------------------
// matchBlock — minimal 5×5 block matching
// ---------------------------------------------------------------------------
bool VideoStabilizer::matchBlock(const uint8_t* prev, const uint8_t* cur,
                                  int w, int h,
                                  int px, int py,
                                  float& outDx, float& outDy)
{
    constexpr int half  = 2;    // patch radius
    constexpr int searchR = 4;  // search radius

    if (px < half + searchR || py < half + searchR ||
        px >= w - half - searchR || py >= h - half - searchR)
        return false;

    int bestDx = 0, bestDy = 0;
    int64_t bestSAD = INT64_MAX;

    for (int dy = -searchR; dy <= searchR; ++dy) {
        for (int dx = -searchR; dx <= searchR; ++dx) {
            int64_t sad = 0;
            for (int ky = -half; ky <= half; ++ky)
                for (int kx = -half; kx <= half; ++kx)
                    sad += std::abs((int)prev[(py+ky)*w+(px+kx)]
                                  - (int)cur[(py+ky+dy)*w+(px+kx+dx)]);
            if (sad < bestSAD) { bestSAD = sad; bestDx = dx; bestDy = dy; }
        }
    }
    outDx = (float)bestDx;
    outDy = (float)bestDy;
    return bestSAD < (25 * 25 * 5); // threshold: low SAD = valid match
}

// ---------------------------------------------------------------------------
// analyzeFrame
// ---------------------------------------------------------------------------
void VideoStabilizer::analyzeFrame(const uint8_t* rgba, int width, int height,
                                    int64_t timeNs)
{
    // Downsample to at most 320×240 for speed
    constexpr int kW = 320, kH = 240;
    std::vector<uint8_t> gray(kW * kH);
    {
        std::vector<uint8_t> small(kW * kH * 4);
        downsample(rgba, width, height, small.data(), kW, kH);
        // Convert small rgba to gray (re-interpret small as rgba per-pixel)
        for (int i = 0; i < kW * kH; ++i) {
            const uint8_t* p = small.data() + i * 4;
            gray[i] = (uint8_t)(0.299f*p[0] + 0.587f*p[1] + 0.114f*p[2]);
        }
    }

    MotionRecord rec;
    rec.timeNs = timeNs;

    if (!m_prevGray.empty() && m_prevW == kW && m_prevH == kH) {
        // Sample grid of points to estimate frame motion
        constexpr int kGridW = 6, kGridH = 5;
        float sumDx = 0.f, sumDy = 0.f;
        int   count = 0;
        for (int gy = 1; gy <= kGridH; ++gy) {
            for (int gx = 1; gx <= kGridW; ++gx) {
                int px = gx * kW / (kGridW + 1);
                int py = gy * kH / (kGridH + 1);
                float dx, dy;
                if (matchBlock(m_prevGray.data(), gray.data(), kW, kH,
                               px, py, dx, dy)) {
                    sumDx += dx; sumDy += dy; ++count;
                }
            }
        }
        if (count > 0) {
            rec.dx = sumDx / count * (float)width  / kW;
            rec.dy = sumDy / count * (float)height / kH;
        }
    }

    m_rawMotions.push_back(rec);
    m_prevGray = std::move(gray);
    m_prevW = kW; m_prevH = kH;
}

// ---------------------------------------------------------------------------
// gaussSmooth
// ---------------------------------------------------------------------------
std::vector<float> VideoStabilizer::gaussSmooth(const std::vector<float>& signal,
                                                  int radius) const
{
    int n = (int)signal.size();
    std::vector<float> out(n, 0.f);
    // Build gaussian kernel
    std::vector<float> kernel(2 * radius + 1);
    float sigma = radius / 2.f;
    float ksum  = 0.f;
    for (int k = -radius; k <= radius; ++k) {
        float v = std::exp(-0.5f * k * k / (sigma * sigma + 1e-6f));
        kernel[k + radius] = v;
        ksum += v;
    }
    for (auto& v : kernel) v /= ksum;

    for (int i = 0; i < n; ++i) {
        float acc = 0.f;
        for (int k = -radius; k <= radius; ++k) {
            int j = std::min(std::max(i + k, 0), n - 1);
            acc += signal[j] * kernel[k + radius];
        }
        out[i] = acc;
    }
    return out;
}

// ---------------------------------------------------------------------------
// computeTrajectory
// ---------------------------------------------------------------------------
void VideoStabilizer::computeTrajectory() {
    int n = (int)m_rawMotions.size();
    if (n == 0) return;

    // Accumulate trajectory
    std::vector<float> trajX(n), trajY(n);
    float cx = 0.f, cy = 0.f;
    for (int i = 0; i < n; ++i) {
        cx += m_rawMotions[i].dx;
        cy += m_rawMotions[i].dy;
        trajX[i] = cx;
        trajY[i] = cy;
    }

    // Smooth trajectory
    auto smoothX = gaussSmooth(trajX, m_smoothRadius);
    auto smoothY = gaussSmooth(trajY, m_smoothRadius);

    // Compute correction = smoothed - raw (correction to apply)
    m_smoothedMotions.resize(n);
    for (int i = 0; i < n; ++i) {
        float corrX = smoothX[i] - trajX[i];
        float corrY = smoothY[i] - trajY[i];
        // Clamp correction
        corrX = std::max(-m_maxCorrectionPx, std::min(m_maxCorrectionPx, corrX));
        corrY = std::max(-m_maxCorrectionPx, std::min(m_maxCorrectionPx, corrY));
        m_smoothedMotions[i].timeNs = m_rawMotions[i].timeNs;
        m_smoothedMotions[i].dx     = corrX;
        m_smoothedMotions[i].dy     = corrY;
    }

    LOGI("VideoStabilizer: computeTrajectory done (%d frames)", n);
}

// ---------------------------------------------------------------------------
// getTransformAt
// ---------------------------------------------------------------------------
StabilizedTransform VideoStabilizer::getTransformAt(int64_t timeNs) const {
    StabilizedTransform t;

    if (m_strategy == Strategy::GYRO ||
        (m_strategy == Strategy::AUTO && !m_gyroSamples.empty()))
    {
        return integrateGyro(timeNs);
    }

    if (m_smoothedMotions.empty()) {
        t.valid = false; return t;
    }

    // Binary search nearest frame
    auto it = std::lower_bound(m_smoothedMotions.begin(), m_smoothedMotions.end(),
        timeNs, [](const MotionRecord& r, int64_t v){ return r.timeNs < v; });

    if (it == m_smoothedMotions.end()) --it;
    if (it != m_smoothedMotions.begin()) {
        auto prev = std::prev(it);
        if (std::abs(prev->timeNs - timeNs) < std::abs(it->timeNs - timeNs))
            it = prev;
    }

    t.dx    = it->dx;
    t.dy    = it->dy;
    t.scale = 1.f + m_cropRatio;
    return t;
}

// ---------------------------------------------------------------------------
// addGyroSample / setGyroSamples
// ---------------------------------------------------------------------------
void VideoStabilizer::addGyroSample(const GyroSample& s) {
    m_gyroSamples.push_back(s);
}
void VideoStabilizer::setGyroSamples(std::vector<GyroSample> samples) {
    m_gyroSamples = std::move(samples);
}

// ---------------------------------------------------------------------------
// integrateGyro — simple integration of angular velocity
// ---------------------------------------------------------------------------
StabilizedTransform VideoStabilizer::integrateGyro(int64_t timeNs) const {
    StabilizedTransform t;
    if (m_gyroSamples.empty()) { t.valid = false; return t; }

    // Find gyro sample at timeNs
    auto it = std::lower_bound(m_gyroSamples.begin(), m_gyroSamples.end(),
        timeNs, [](const GyroSample& s, int64_t v){ return s.timestampNs < v; });

    if (it == m_gyroSamples.end()) it = std::prev(m_gyroSamples.end());

    // Accumulate rotation from start to timeNs
    float angleX = 0.f, angleY = 0.f;
    for (auto s = m_gyroSamples.begin(); s != it; ++s) {
        auto next = std::next(s);
        if (next == m_gyroSamples.end()) break;
        float dt = (next->timestampNs - s->timestampNs) * 1e-9f;
        angleX += s->gx * dt;
        angleY += s->gy * dt;
    }

    // Smooth angles (simple exponential filter)
    static float smoothAngleX = 0.f, smoothAngleY = 0.f;
    constexpr float alpha = 0.1f;
    smoothAngleX = smoothAngleX * (1.f - alpha) + angleX * alpha;
    smoothAngleY = smoothAngleY * (1.f - alpha) + angleY * alpha;

    // Convert angle to pixel displacement (approximate focal length)
    constexpr float kFocalPx = 1000.f;
    t.dx    = -(angleX - smoothAngleX) * kFocalPx;
    t.dy    = -(angleY - smoothAngleY) * kFocalPx;
    t.scale = 1.f + m_cropRatio;
    return t;
}

} // namespace ai
} // namespace video
} // namespace sdk
