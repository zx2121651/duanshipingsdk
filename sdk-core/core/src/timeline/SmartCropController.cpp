/**
 * SmartCropController.cpp
 */

#include "../../include/timeline/SmartCropController.h"
#include <cmath>
#include <algorithm>

namespace sdk {
namespace video {
namespace timeline {

SmartCropController::SmartCropController() {
    getRatioWH(m_ratio, m_ratioW, m_ratioH);
}

void SmartCropController::setAspectRatio(AspectRatio ratio) {
    m_ratio = ratio;
    getRatioWH(ratio, m_ratioW, m_ratioH);
}

void SmartCropController::setCustomAspectRatio(float w, float h) {
    m_ratio  = AspectRatio::CUSTOM;
    m_ratioW = w; m_ratioH = h;
}

void SmartCropController::getRatioWH(AspectRatio r, float& w, float& h) {
    switch (r) {
    case AspectRatio::RATIO_9_16:  w=9.f;  h=16.f; break;
    case AspectRatio::RATIO_16_9:  w=16.f; h=9.f;  break;
    case AspectRatio::RATIO_1_1:   w=1.f;  h=1.f;  break;
    case AspectRatio::RATIO_4_3:   w=4.f;  h=3.f;  break;
    case AspectRatio::RATIO_3_4:   w=3.f;  h=4.f;  break;
    default:                        w=9.f;  h=16.f; break;
    }
}

void SmartCropController::reset() {
    m_initialized = false;
    m_smoothCX = 0.5f;
    m_smoothCY = 0.4f;
    m_lastSubjects.clear();
    m_currentCrop = {};
}

void SmartCropController::updateDetection(const std::vector<SubjectRegion>& subjects) {
    m_lastSubjects = subjects;
}

// ---------------------------------------------------------------------------
CropRect SmartCropController::buildCropRect(float cx, float cy,
                                             float frameAspect) const
{
    // Target aspect ratio
    float targetAspect = m_ratioW / m_ratioH;

    // Crop size in normalized coords
    float cropW, cropH;
    if (frameAspect >= targetAspect) {
        // Frame is wider than target → full height, crop width
        cropH = 1.f;
        cropW = targetAspect / frameAspect;
    } else {
        // Frame is taller than target → full width, crop height
        cropW = 1.f;
        cropH = frameAspect / targetAspect;
    }

    // Apply rule-of-thirds: offset center slightly toward 1/3 line
    float adjCX = cx, adjCY = cy;
    if (m_ruleOfThirds) {
        // Snap center toward nearest vertical third
        float third1 = 1.f / 3.f, third2 = 2.f / 3.f;
        float dist1 = std::abs(cx - third1), dist2 = std::abs(cx - third2);
        float snapX = (dist1 < dist2) ? third1 : third2;
        adjCX = cx + (snapX - cx) * 0.25f;  // 25% pull toward third

        // Horizontal third for Y (subject slightly above center)
        adjCY = cy + (third1 - cy) * 0.15f;
    }

    // Place crop rect centered on subject
    float x = adjCX - cropW * 0.5f;
    float y = adjCY - cropH * 0.5f;

    // Clamp with edge padding
    float pad = m_edgePadding;
    x = std::max(pad, std::min(1.f - cropW - pad, x));
    y = std::max(pad, std::min(1.f - cropH - pad, y));

    return {x, y, cropW, cropH};
}

// ---------------------------------------------------------------------------
CropRect SmartCropController::compute(int frameW, int frameH) {
    float frameAspect = (frameH > 0) ? (float)frameW / frameH : 1.f;

    // Find best subject
    float targetCX = 0.5f, targetCY = 0.4f;
    bool found = false;

    // Priority: preferred type first, then highest confidence
    for (const auto& s : m_lastSubjects) {
        if (s.type == m_preferredType) {
            targetCX = s.rect.x + s.rect.w * 0.5f;
            targetCY = s.rect.y + s.rect.h * 0.5f;
            found = true;
            break;
        }
    }
    if (!found && !m_lastSubjects.empty()) {
        auto best = std::max_element(m_lastSubjects.begin(), m_lastSubjects.end(),
            [](const SubjectRegion& a, const SubjectRegion& b) {
                return a.confidence < b.confidence;
            });
        targetCX = best->rect.x + best->rect.w * 0.5f;
        targetCY = best->rect.y + best->rect.h * 0.5f;
    }

    // Initialize smoothed center on first call
    if (!m_initialized) {
        m_smoothCX    = targetCX;
        m_smoothCY    = targetCY;
        m_initialized = true;
    }

    // Exponential smoothing
    float alpha = 1.f - m_smoothFactor;
    m_smoothCX = m_smoothCX * m_smoothFactor + targetCX * alpha;
    m_smoothCY = m_smoothCY * m_smoothFactor + targetCY * alpha;

    m_currentCrop = buildCropRect(m_smoothCX, m_smoothCY, frameAspect);
    return m_currentCrop;
}

} // namespace timeline
} // namespace video
} // namespace sdk
