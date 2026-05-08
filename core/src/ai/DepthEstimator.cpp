/**
 * DepthEstimator.cpp
 *
 * 深度估计实现：
 *   - 梯度近似（内置 CPU fallback）
 *   - 后端注入（TFLite MiDaS 等）
 */

#include "../../include/ai/DepthEstimator.h"

#define LOG_TAG "DepthEstimator"
#include "../../include/Log.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
DepthEstimator::DepthEstimator() = default;

void DepthEstimator::setBackend(std::shared_ptr<IDepthBackend> backend) {
    m_backend = std::move(backend);
    if (m_backend)
        LOGI("DepthEstimator: backend = %s", m_backend->name());
}

// ---------------------------------------------------------------------------
// estimate() — dispatch to backend or gradient fallback
// ---------------------------------------------------------------------------
DepthMap DepthEstimator::estimate(const uint8_t* rgba, int w, int h,
                                   int outW, int outH)
{
    if (outW <= 0) outW = w;
    if (outH <= 0) outH = h;

    if (m_backend) {
        auto dm = m_backend->estimate(rgba, w, h);
        if (dm.valid) {
            if (dm.width != outW || dm.height != outH)
                return resize(dm, outW, outH);
            return dm;
        }
        LOGW("DepthEstimator: backend failed, falling back to gradient");
    }
    return estimateGradient(rgba, w, h, outW, outH);
}

// ---------------------------------------------------------------------------
// Gradient-based approximate depth (CPU fallback).
// 
// Theory: in natural images, defocused regions have lower gradient magnitude.
// We use inverse gradient magnitude (blurred) as a proxy for "farness".
// Not accurate, but provides plausible bokeh masks for non-face regions.
// ---------------------------------------------------------------------------
DepthMap DepthEstimator::estimateGradient(const uint8_t* rgba, int w, int h,
                                           int outW, int outH) const
{
    // 1. Convert to grayscale
    std::vector<float> gray(w * h);
    for (int i = 0; i < w*h; ++i) {
        gray[i] = (0.299f*rgba[i*4] + 0.587f*rgba[i*4+1] + 0.114f*rgba[i*4+2]) / 255.f;
    }

    // 2. Sobel gradient magnitude
    DepthMap dm;
    dm.width  = outW;
    dm.height = outH;
    dm.data.resize(outW * outH, 0.f);
    dm.valid  = true;

    auto grayAt = [&](int x, int y) -> float {
        x = std::max(0, std::min(w-1, x));
        y = std::max(0, std::min(h-1, y));
        return gray[y*w+x];
    };

    std::vector<float> grad(w * h);
    for (int y=1;y<h-1;++y)
        for (int x=1;x<w-1;++x) {
            float gx = grayAt(x+1,y-1)+2*grayAt(x+1,y)+grayAt(x+1,y+1)
                      -grayAt(x-1,y-1)-2*grayAt(x-1,y)-grayAt(x-1,y+1);
            float gy = grayAt(x-1,y+1)+2*grayAt(x,y+1)+grayAt(x+1,y+1)
                      -grayAt(x-1,y-1)-2*grayAt(x,y-1)-grayAt(x+1,y-1);
            grad[y*w+x] = std::sqrt(gx*gx+gy*gy) * 0.25f;
        }

    // 3. Box blur gradient (approximates defocus)
    const int kRadius = std::max(1, w / 20);
    std::vector<float> blurred(w*h, 0.f);
    for (int y=0;y<h;++y)
        for (int x=0;x<w;++x) {
            float sum=0.f; int cnt=0;
            for (int dy=-kRadius;dy<=kRadius;++dy)
                for (int dx=-kRadius;dx<=kRadius;++dx) {
                    int nx=x+dx, ny=y+dy;
                    if (nx>=0&&nx<w&&ny>=0&&ny<h) { sum+=grad[ny*w+nx]; ++cnt; }
                }
            blurred[y*w+x] = (cnt>0) ? sum/cnt : 0.f;
        }

    // 4. Inverse gradient → depth (high gradient = near/in-focus region)
    float maxG = *std::max_element(blurred.begin(), blurred.end());
    if (maxG < 1e-6f) maxG = 1.f;
    for (int i=0;i<w*h;++i) blurred[i] = 1.f - blurred[i]/maxG;

    // 5. Resize to output
    if (outW == w && outH == h) {
        dm.data = std::move(blurred);
    } else {
        DepthMap src; src.width=w; src.height=h; src.data=blurred; src.valid=true;
        return resize(src, outW, outH);
    }
    return dm;
}

// ---------------------------------------------------------------------------
// resize — bilinear interpolation
// ---------------------------------------------------------------------------
float DepthEstimator::lerpDepth(const DepthMap& src, float x, float y) {
    int x0=(int)x, y0=(int)y;
    int x1=std::min(x0+1,src.width-1), y1=std::min(y0+1,src.height-1);
    float fx=x-x0, fy=y-y0;
    float v00=src.at(x0,y0), v10=src.at(x1,y0);
    float v01=src.at(x0,y1), v11=src.at(x1,y1);
    return v00*(1-fx)*(1-fy) + v10*fx*(1-fy) + v01*(1-fx)*fy + v11*fx*fy;
}

DepthMap DepthEstimator::resize(const DepthMap& src, int dstW, int dstH) {
    DepthMap out; out.width=dstW; out.height=dstH; out.data.resize(dstW*dstH); out.valid=true;
    for (int y=0;y<dstH;++y)
        for (int x=0;x<dstW;++x) {
            float sx=(x+0.5f)*src.width/dstW - 0.5f;
            float sy=(y+0.5f)*src.height/dstH - 0.5f;
            out.data[y*dstW+x] = lerpDepth(src, sx, sy);
        }
    return out;
}

// ---------------------------------------------------------------------------
// buildBokehMask — smooth foreground/background separation
// ---------------------------------------------------------------------------
std::vector<float> DepthEstimator::buildBokehMask(const DepthMap& dm,
                                                    float focalDepth,
                                                    float dofRadius)
{
    std::vector<float> mask(dm.width * dm.height);
    for (int i=0;i<(int)dm.data.size();++i) {
        float d = dm.data[i];
        float dist = std::abs(d - focalDepth);
        if (dist <= dofRadius) {
            mask[i] = 1.f;  // in focus (foreground)
        } else {
            // Smooth falloff: 1 within DOF, 0 at 2*radius
            float t = (dist - dofRadius) / dofRadius;
            mask[i] = std::max(0.f, 1.f - t);
        }
    }
    return mask;
}

// ---------------------------------------------------------------------------
std::vector<uint8_t> DepthEstimator::toColormap(const DepthMap& dm) {
    std::vector<uint8_t> out(dm.width * dm.height * 4);
    for (int i=0;i<(int)dm.data.size();++i) {
        float d = dm.data[i];
        // Blue(near) → Green → Red(far)
        uint8_t r,g,b;
        if (d < 0.5f) { float t=d*2.f; r=0; g=(uint8_t)(t*255); b=(uint8_t)((1-t)*255); }
        else           { float t=(d-0.5f)*2.f; r=(uint8_t)(t*255); g=(uint8_t)((1-t)*255); b=0; }
        out[i*4]=r; out[i*4+1]=g; out[i*4+2]=b; out[i*4+3]=255;
    }
    return out;
}

float DepthEstimator::minDepth(const DepthMap& dm) const {
    if (dm.data.empty()) return 0.f;
    return *std::min_element(dm.data.begin(), dm.data.end());
}
float DepthEstimator::maxDepth(const DepthMap& dm) const {
    if (dm.data.empty()) return 0.f;
    return *std::max_element(dm.data.begin(), dm.data.end());
}
float DepthEstimator::meanDepth(const DepthMap& dm) const {
    if (dm.data.empty()) return 0.f;
    float sum = std::accumulate(dm.data.begin(), dm.data.end(), 0.f);
    return sum / dm.data.size();
}

} // namespace ai
} // namespace video
} // namespace sdk
