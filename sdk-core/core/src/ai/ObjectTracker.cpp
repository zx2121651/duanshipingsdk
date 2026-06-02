/**
 * ObjectTracker.cpp
 */

#include "../../include/ai/ObjectTracker.h"

#define LOG_TAG "ObjectTracker"
#include "../../include/Log.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
ObjectTracker::ObjectTracker() = default;

void ObjectTracker::reset() {
    m_initialized = false;
    m_frameIdx    = 0;
    m_tmpl.clear();
    m_tmplW = m_tmplH = 0;
    m_currentRect = {};
    std::memset(m_histH, 0, sizeof(m_histH));
    std::memset(m_histS, 0, sizeof(m_histS));
}

// ---------------------------------------------------------------------------
// Pixel helpers
// ---------------------------------------------------------------------------
void ObjectTracker::rgbaAt(const uint8_t* rgba, int w, int h,
                             int px, int py, float& r, float& g, float& b)
{
    px = std::max(0, std::min(w-1, px));
    py = std::max(0, std::min(h-1, py));
    const uint8_t* p = rgba + (py * w + px) * 4;
    r = p[0] / 255.f; g = p[1] / 255.f; b = p[2] / 255.f;
}

void ObjectTracker::rgb2hsv(float r, float g, float b, float& h, float& s, float& v) {
    float mx = std::max({r,g,b}), mn = std::min({r,g,b});
    float d  = mx - mn;
    v = mx;
    s = (mx < 1e-6f) ? 0.f : d / mx;
    if (d < 1e-6f) { h = 0.f; return; }
    if (mx == r)      h = 60.f * std::fmod((g-b)/d, 6.f);
    else if (mx == g) h = 60.f * ((b-r)/d + 2.f);
    else              h = 60.f * ((r-g)/d + 4.f);
    if (h < 0.f) h += 360.f;
}

// ---------------------------------------------------------------------------
// ROI conversions
// ---------------------------------------------------------------------------
TrackRect ObjectTracker::toNorm(int px, int py, int pw, int ph, int W, int H) const {
    return {px/(float)W, py/(float)H, pw/(float)W, ph/(float)H};
}
void ObjectTracker::fromNorm(const TrackRect& r, int W, int H,
                              int& px, int& py, int& pw, int& ph) const
{
    px = (int)(r.x * W); py = (int)(r.y * H);
    pw = std::max(1, (int)(r.w * W));
    ph = std::max(1, (int)(r.h * H));
}

// ---------------------------------------------------------------------------
// toGray / downsample
// ---------------------------------------------------------------------------
void ObjectTracker::toGray(const uint8_t* rgba, int w, int h,
                             std::vector<float>& gray)
{
    int n = w * h;
    gray.resize(n);
    for (int i = 0; i < n; ++i) {
        const uint8_t* p = rgba + i*4;
        gray[i] = 0.299f*p[0] + 0.587f*p[1] + 0.114f*p[2];
    }
}

void ObjectTracker::downsample(const std::vector<float>& src, int sw, int sh,
                                std::vector<float>& dst, int dw, int dh)
{
    dst.resize(dw * dh);
    for (int dy = 0; dy < dh; ++dy)
        for (int dx = 0; dx < dw; ++dx) {
            int sy = dy * sh / dh, sx = dx * sw / dw;
            dst[dy*dw+dx] = src[sy*sw+sx];
        }
}

// ---------------------------------------------------------------------------
// buildHistogram
// ---------------------------------------------------------------------------
void ObjectTracker::buildHistogram(const uint8_t* rgba, int w, int h,
                                    const TrackRect& roi)
{
    std::memset(m_histH, 0, sizeof(m_histH));
    std::memset(m_histS, 0, sizeof(m_histS));
    int rx, ry, rw, rh;
    fromNorm(roi, w, h, rx, ry, rw, rh);
    int count = 0;
    for (int py = ry; py < ry+rh && py < h; ++py) {
        for (int px = rx; px < rx+rw && px < w; ++px) {
            float r, g, b, hue, sat, val;
            rgbaAt(rgba, w, h, px, py, r, g, b);
            rgb2hsv(r, g, b, hue, sat, val);
            if (val > 0.1f) { // ignore very dark pixels
                m_histH[hueBin(hue)]++;
                m_histS[satBin(sat)]++;
                ++count;
            }
        }
    }
    if (count > 0) {
        for (int i = 0; i < kHistBins; ++i) { m_histH[i]/=count; m_histS[i]/=count; }
    }
}

// ---------------------------------------------------------------------------
// templateMatch — normalized cross correlation
// ---------------------------------------------------------------------------
float ObjectTracker::templateMatch(const std::vector<float>& frame, int fw, int fh,
                                    int searchX, int searchY, int searchW, int searchH,
                                    float& outDx, float& outDy) const
{
    if (m_tmpl.empty()) return 0.f;

    // Compute template mean
    float tmplMean = 0.f;
    for (float v : m_tmpl) tmplMean += v;
    tmplMean /= m_tmpl.size();

    float bestNCC = -1.f;
    outDx = outDy = 0.f;

    int tw = m_tmplW, th = m_tmplH;
    // Stride 2 for speed
    for (int sy = searchY; sy < searchY+searchH-th; sy += 2) {
        for (int sx = searchX; sx < searchX+searchW-tw; sx += 2) {
            // Compute patch mean
            float patchMean = 0.f;
            for (int ky = 0; ky < th; ++ky)
                for (int kx = 0; kx < tw; ++kx) {
                    int fx = std::min(sx+kx, fw-1);
                    int fy = std::min(sy+ky, fh-1);
                    patchMean += frame[fy*fw+fx];
                }
            patchMean /= (tw * th);

            float num = 0.f, denA = 0.f, denB = 0.f;
            for (int ky = 0; ky < th; ++ky) {
                for (int kx = 0; kx < tw; ++kx) {
                    int fx = std::min(sx+kx, fw-1);
                    int fy = std::min(sy+ky, fh-1);
                    float tv = m_tmpl[ky*tw+kx] - tmplMean;
                    float fv = frame[fy*fw+fx]  - patchMean;
                    num  += tv * fv;
                    denA += tv * tv;
                    denB += fv * fv;
                }
            }
            float denom = std::sqrt(denA * denB) + 1e-8f;
            float ncc   = num / denom;
            if (ncc > bestNCC) {
                bestNCC = ncc;
                outDx   = (float)(sx - searchX);
                outDy   = (float)(sy - searchY);
            }
        }
    }
    return bestNCC;
}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------
bool ObjectTracker::init(const uint8_t* rgba, int w, int h, const TrackRect& roi) {
    reset();
    if (!roi.valid()) return false;

    m_currentRect = roi;
    m_dw = w / kScale;
    m_dh = h / kScale;

    // Extract template patch (downsampled grayscale)
    int rx, ry, rw, rh;
    fromNorm(roi, w, h, rx, ry, rw, rh);
    m_tmplW = std::max(1, rw / kScale);
    m_tmplH = std::max(1, rh / kScale);
    m_tmpl.resize(m_tmplW * m_tmplH);

    std::vector<float> gray;
    toGray(rgba, w, h, gray);
    std::vector<float> dsGray;
    downsample(gray, w, h, dsGray, m_dw, m_dh);

    int drx = rx/kScale, dry = ry/kScale;
    for (int dy = 0; dy < m_tmplH; ++dy)
        for (int dx = 0; dx < m_tmplW; ++dx) {
            int px = std::min(drx+dx, m_dw-1);
            int py = std::min(dry+dy, m_dh-1);
            m_tmpl[dy*m_tmplW+dx] = dsGray[py*m_dw+px];
        }

    // Color histogram
    buildHistogram(rgba, w, h, roi);

    m_initialized = true;
    LOGI("ObjectTracker: initialized at (%.2f,%.2f,%.2f,%.2f)",
         roi.x, roi.y, roi.w, roi.h);
    return true;
}

// ---------------------------------------------------------------------------
// update()
// ---------------------------------------------------------------------------
TrackResult ObjectTracker::update(const uint8_t* rgba, int w, int h) {
    TrackResult result;
    result.frameIdx = ++m_frameIdx;

    if (!m_initialized) return result;

    std::vector<float> gray;
    toGray(rgba, w, h, gray);
    std::vector<float> dsGray;
    downsample(gray, w, h, dsGray, m_dw, m_dh);

    int rx, ry, rw, rh;
    fromNorm(m_currentRect, w, h, rx, ry, rw, rh);

    // Search region: expand current rect by searchRadius
    int sr = m_searchRadius;
    int sx = std::max(0, rx/kScale - sr/kScale);
    int sy = std::max(0, ry/kScale - sr/kScale);
    int sw = std::min(m_dw - sx, rw/kScale + 2*(sr/kScale));
    int sh = std::min(m_dh - sy, rh/kScale + 2*(sr/kScale));

    float dx = 0.f, dy = 0.f;
    float ncc = templateMatch(dsGray, m_dw, m_dh, sx, sy, sw, sh, dx, dy);

    float confidence = (ncc + 1.f) * 0.5f;  // NCC [-1,1] → [0,1]
    result.confidence = confidence;

    if (confidence >= m_confThreshold) {
        // Update position
        int newX = (int)((sx + dx) * kScale);
        int newY = (int)((sy + dy) * kScale);
        m_currentRect = toNorm(newX, newY, rw, rh, w, h);
        result.found = true;
        result.rect  = m_currentRect;
    } else if (m_autoReinit && m_detector) {
        TrackRect detected = m_detector(rgba, w, h);
        if (detected.valid()) {
            init(rgba, w, h, detected);
            result.found = true;
            result.rect  = detected;
            result.confidence = 0.5f;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// meanShiftUpdate (minimal stub — used as refinement in HYBRID mode)
// ---------------------------------------------------------------------------
TrackResult ObjectTracker::meanShiftUpdate(const uint8_t* rgba, int w, int h) {
    TrackResult result;
    result.frameIdx = m_frameIdx;
    if (!m_initialized) return result;

    int rx, ry, rw, rh;
    fromNorm(m_currentRect, w, h, rx, ry, rw, rh);

    // Mean shift: iterate candidate center
    float cx = rx + rw * 0.5f, cy = ry + rh * 0.5f;

    for (int iter = 0; iter < 10; ++iter) {
        float sumX = 0.f, sumY = 0.f, sumW = 0.f;
        for (int py = (int)(cy-rh/2); py < (int)(cy+rh/2) && py < h; ++py) {
            for (int px = (int)(cx-rw/2); px < (int)(cx+rw/2) && px < w; ++px) {
                if (px < 0 || py < 0) continue;
                float r, g, b, hue, sat, val;
                rgbaAt(rgba, w, h, px, py, r, g, b);
                rgb2hsv(r, g, b, hue, sat, val);
                float weight = m_histH[hueBin(hue)] * m_histS[satBin(sat)];
                sumX += px * weight;
                sumY += py * weight;
                sumW += weight;
            }
        }
        if (sumW < 1e-6f) break;
        float ncx = sumX / sumW, ncy = sumY / sumW;
        float shift = std::sqrt((ncx-cx)*(ncx-cx)+(ncy-cy)*(ncy-cy));
        cx = ncx; cy = ncy;
        if (shift < 1.f) break;
    }

    int newX = (int)(cx - rw*0.5f), newY = (int)(cy - rh*0.5f);
    m_currentRect = toNorm(newX, newY, rw, rh, w, h);
    result.found = true;
    result.rect  = m_currentRect;
    result.confidence = 0.6f;
    return result;
}

} // namespace ai
} // namespace video
} // namespace sdk
