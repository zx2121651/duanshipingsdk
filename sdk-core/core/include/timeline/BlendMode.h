#pragma once
/**
 * BlendMode.h
 *
 * 图层混合模式枚举与 GLSL 片段着色器实现。
 *
 * 支持模式（与 Photoshop/After Effects 命名一致）：
 *   NORMAL      标准 alpha 合成（Porter-Duff SRC_OVER）
 *   MULTIPLY    正片叠底
 *   SCREEN      滤色
 *   OVERLAY     叠加
 *   SOFT_LIGHT  柔光
 *   HARD_LIGHT  强光
 *   COLOR_DODGE 颜色减淡
 *   COLOR_BURN  颜色加深
 *   DARKEN      变暗
 *   LIGHTEN     变亮
 *   DIFFERENCE  差值
 *   EXCLUSION   排除
 *   HUE         色相
 *   SATURATION  饱和度
 *   COLOR       颜色
 *   LUMINOSITY  明度
 *   ADD         线性减淡（相加）
 *
 * 用法（Compositor / Track 层设置）：
 *   track->setBlendMode(BlendMode::MULTIPLY);
 *   track->setOpacity(0.8f);
 */

#include <string>

namespace sdk {
namespace video {
namespace timeline {

enum class BlendMode : int {
    NORMAL      = 0,
    MULTIPLY    = 1,
    SCREEN      = 2,
    OVERLAY     = 3,
    SOFT_LIGHT  = 4,
    HARD_LIGHT  = 5,
    COLOR_DODGE = 6,
    COLOR_BURN  = 7,
    DARKEN      = 8,
    LIGHTEN     = 9,
    DIFFERENCE  = 10,
    EXCLUSION   = 11,
    HUE         = 12,
    SATURATION  = 13,
    COLOR       = 14,
    LUMINOSITY  = 15,
    ADD         = 16,
};

/** 返回 BlendMode 的可读名称（调试 / 序列化用）。 */
inline const char* blendModeName(BlendMode m) {
    switch (m) {
        case BlendMode::NORMAL:      return "normal";
        case BlendMode::MULTIPLY:    return "multiply";
        case BlendMode::SCREEN:      return "screen";
        case BlendMode::OVERLAY:     return "overlay";
        case BlendMode::SOFT_LIGHT:  return "soft_light";
        case BlendMode::HARD_LIGHT:  return "hard_light";
        case BlendMode::COLOR_DODGE: return "color_dodge";
        case BlendMode::COLOR_BURN:  return "color_burn";
        case BlendMode::DARKEN:      return "darken";
        case BlendMode::LIGHTEN:     return "lighten";
        case BlendMode::DIFFERENCE:  return "difference";
        case BlendMode::EXCLUSION:   return "exclusion";
        case BlendMode::HUE:         return "hue";
        case BlendMode::SATURATION:  return "saturation";
        case BlendMode::COLOR:       return "color";
        case BlendMode::LUMINOSITY:  return "luminosity";
        case BlendMode::ADD:         return "add";
        default:                     return "unknown";
    }
}

inline BlendMode blendModeFromString(const std::string& s) {
    if (s == "multiply")    return BlendMode::MULTIPLY;
    if (s == "screen")      return BlendMode::SCREEN;
    if (s == "overlay")     return BlendMode::OVERLAY;
    if (s == "soft_light")  return BlendMode::SOFT_LIGHT;
    if (s == "hard_light")  return BlendMode::HARD_LIGHT;
    if (s == "color_dodge") return BlendMode::COLOR_DODGE;
    if (s == "color_burn")  return BlendMode::COLOR_BURN;
    if (s == "darken")      return BlendMode::DARKEN;
    if (s == "lighten")     return BlendMode::LIGHTEN;
    if (s == "difference")  return BlendMode::DIFFERENCE;
    if (s == "exclusion")   return BlendMode::EXCLUSION;
    if (s == "hue")         return BlendMode::HUE;
    if (s == "saturation")  return BlendMode::SATURATION;
    if (s == "color")       return BlendMode::COLOR;
    if (s == "luminosity")  return BlendMode::LUMINOSITY;
    if (s == "add")         return BlendMode::ADD;
    return BlendMode::NORMAL;
}

// ---------------------------------------------------------------------------
// GLSL snippet — paste into any fragment shader that needs blend modes.
// Exposes: vec3 applyBlendMode(int mode, vec3 bg, vec3 fg)
// ---------------------------------------------------------------------------
inline const char* blendModeGlslFunctions() {
    return R"(
// ── Blend Mode helpers ────────────────────────────────────────────────────
vec3 blendMultiply(vec3 bg, vec3 fg)   { return bg * fg; }
vec3 blendScreen  (vec3 bg, vec3 fg)   { return 1.0 - (1.0-bg)*(1.0-fg); }
vec3 blendOverlay (vec3 bg, vec3 fg) {
    return mix(2.0*bg*fg, 1.0 - 2.0*(1.0-bg)*(1.0-fg),
               step(0.5, bg));
}
vec3 blendSoftLight(vec3 bg, vec3 fg) {
    return mix(bg - (1.0-2.0*fg)*bg*(1.0-bg),
               bg + (2.0*fg-1.0)*(sqrt(bg)-bg),
               step(0.5, fg));
}
vec3 blendHardLight(vec3 bg, vec3 fg)   { return blendOverlay(fg, bg); }
vec3 blendColorDodge(vec3 bg, vec3 fg)  { return min(bg / max(1.0-fg, 0.001), 1.0); }
vec3 blendColorBurn (vec3 bg, vec3 fg)  { return 1.0 - min((1.0-bg) / max(fg, 0.001), 1.0); }
vec3 blendDarken   (vec3 bg, vec3 fg)   { return min(bg, fg); }
vec3 blendLighten  (vec3 bg, vec3 fg)   { return max(bg, fg); }
vec3 blendDifference(vec3 bg, vec3 fg)  { return abs(bg - fg); }
vec3 blendExclusion (vec3 bg, vec3 fg)  { return bg + fg - 2.0*bg*fg; }
vec3 blendAdd       (vec3 bg, vec3 fg)  { return min(bg + fg, 1.0); }

// HSL helpers for hue/sat/color/luma blend
float blendLuminosity1(vec3 c)  { return dot(c, vec3(0.299,0.587,0.114)); }
vec3  setLuminosity(vec3 c, float lum) {
    float d = lum - blendLuminosity1(c);
    c += d;
    float mn = min(c.r,min(c.g,c.b)), mx = max(c.r,max(c.g,c.b));
    if (mn < 0.0) c = lum + (c-lum)*lum/max(lum-mn,0.001);
    if (mx > 1.0) c = lum + (c-lum)*(1.0-lum)/max(mx-lum,0.001);
    return c;
}
float blendSaturation1(vec3 c) { return max(c.r,max(c.g,c.b)) - min(c.r,min(c.g,c.b)); }
vec3  setSaturation(vec3 c, float sat) {
    float cmin=min(c.r,min(c.g,c.b)), cmax=max(c.r,max(c.g,c.b));
    float cs = cmax - cmin;
    if (cs > 0.0001) c = (c - cmin) * sat / cs;
    else c = vec3(0.0);
    return c;
}

// mode constants: NORMAL=0 MULTIPLY=1 SCREEN=2 OVERLAY=3 SOFT_LIGHT=4
//   HARD_LIGHT=5 COLOR_DODGE=6 COLOR_BURN=7 DARKEN=8 LIGHTEN=9
//   DIFFERENCE=10 EXCLUSION=11 HUE=12 SATURATION=13 COLOR=14 LUMINOSITY=15 ADD=16
vec3 applyBlendMode(int mode, vec3 bg, vec3 fg) {
    if      (mode == 1)  return blendMultiply(bg, fg);
    else if (mode == 2)  return blendScreen(bg, fg);
    else if (mode == 3)  return blendOverlay(bg, fg);
    else if (mode == 4)  return blendSoftLight(bg, fg);
    else if (mode == 5)  return blendHardLight(bg, fg);
    else if (mode == 6)  return blendColorDodge(bg, fg);
    else if (mode == 7)  return blendColorBurn(bg, fg);
    else if (mode == 8)  return blendDarken(bg, fg);
    else if (mode == 9)  return blendLighten(bg, fg);
    else if (mode == 10) return blendDifference(bg, fg);
    else if (mode == 11) return blendExclusion(bg, fg);
    else if (mode == 12) return setLuminosity(setSaturation(bg, blendSaturation1(bg)),
                                              blendLuminosity1(bg)); // HUE: use fg hue, bg sat+lum
    else if (mode == 13) return setLuminosity(setSaturation(bg, blendSaturation1(fg)),
                                              blendLuminosity1(bg));
    else if (mode == 14) return setLuminosity(fg, blendLuminosity1(bg));
    else if (mode == 15) return setLuminosity(bg, blendLuminosity1(fg));
    else if (mode == 16) return blendAdd(bg, fg);
    else                 return fg;  // NORMAL
}
)";
}

} // namespace timeline
} // namespace video
} // namespace sdk
