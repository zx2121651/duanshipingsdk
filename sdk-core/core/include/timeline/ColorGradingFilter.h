#pragma once
/**
 * ColorGradingFilter.h
 *
 * 专业色彩分级滤镜，基于现有 LUT3D 管线扩展。
 *
 * 功能层级（可独立启用/禁用）：
 *   1. 基础调色  — 亮度/对比度/饱和度/色温/色调
 *   2. RGB 曲线  — 独立 R/G/B/Master 256点 Bezier 曲线
 *   3. HSL 调色  — 按色相段独立调整饱和度/亮度/色相偏移
 *   4. 色轮      — 阴影/中间调/高光 色轮偏移（影视级调色）
 *   5. LUT 3D    — 加载 .cube / 自定义 LUT 纹理（已有 LUT3DFilter 基础）
 *
 * 全部计算在 GLSL 片元着色器中完成（单 pass），性能友好。
 *
 * 用法：
 *   ColorGradingFilter cg;
 *   cg.setBrightness(0.05f);
 *   cg.setSaturation(1.2f);
 *   cg.setColorWheelShadows(-0.05f, 0.02f);  // cyan shadows
 *   cg.loadLutFromFile("cinematic_a.cube");
 */

#include "../Filter.h"
#include <string>
#include <array>
#include <vector>
#include <cstdint>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// RGB 曲线（256 点查找表，输入 → 输出，均为 [0,255]）
// ---------------------------------------------------------------------------
struct RGBCurve {
    std::array<uint8_t, 256> r{}, g{}, b{}, master{};
    RGBCurve() {
        for (int i = 0; i < 256; ++i) r[i] = g[i] = b[i] = master[i] = (uint8_t)i;
    }
    bool isIdentity() const {
        for (int i = 0; i < 256; ++i)
            if (r[i]!=(uint8_t)i || g[i]!=(uint8_t)i || b[i]!=(uint8_t)i
             || master[i]!=(uint8_t)i) return false;
        return true;
    }
};

// ---------------------------------------------------------------------------
// HSL 分段调整（8色相段）
// ---------------------------------------------------------------------------
struct HSLAdjustment {
    struct Segment {
        float hueShift = 0.f;   ///< [-180, +180] 度
        float satScale = 1.f;   ///< [0, 3.0]
        float lumShift = 0.f;   ///< [-1, +1]
    };
    // 0=红 1=橙 2=黄 3=绿 4=青 5=蓝 6=紫 7=洋红
    std::array<Segment, 8> segments{};
};

// ---------------------------------------------------------------------------
// 色轮（阴影/中间调/高光）
// ---------------------------------------------------------------------------
struct ColorWheels {
    float shadowLift[3]    = {0,0,0}; ///< RGB offset in shadows [-0.2, 0.2]
    float midtoneBalance[3]= {0,0,0}; ///< RGB in midtones
    float highlightGain[3] = {1,1,1}; ///< RGB gain in highlights [0.5, 2.0]
};

// ---------------------------------------------------------------------------
// ColorGradingFilter
// ---------------------------------------------------------------------------
class ColorGradingFilter : public Filter {
public:
    ColorGradingFilter();
    ~ColorGradingFilter() override;

    Result initialize() override;
    void   onProgramRecompiled() override;

    // ── 基础调色 ──────────────────────────────────────────────────────────

    /** 亮度偏移 [-1, +1]，默认 0。 */
    void setBrightness(float v)    { m_brightness = v;    m_shaderDirty = true; }

    /** 对比度缩放 [0, 3]，默认 1。 */
    void setContrast(float v)      { m_contrast   = v;    m_shaderDirty = true; }

    /** 饱和度缩放 [0, 3]，默认 1。 */
    void setSaturation(float v)    { m_saturation = v;    m_shaderDirty = true; }

    /** 色温偏移 [-1(冷), +1(暖)]，默认 0。 */
    void setTemperature(float v)   { m_temperature= v;    m_shaderDirty = true; }

    /** 色调偏移 [-1(绿), +1(洋红)]，默认 0。 */
    void setTint(float v)          { m_tint       = v;    m_shaderDirty = true; }

    /** 曝光（EV）[-3, +3]，默认 0。 */
    void setExposure(float ev)     { m_exposure   = ev;   m_shaderDirty = true; }

    /** 白点（高光压缩，0=不压缩）。 */
    void setHighlights(float v)    { m_highlights = v;    m_shaderDirty = true; }

    /** 黑点（阴影提升，0=不提升）。 */
    void setShadows(float v)       { m_shadows    = v;    m_shaderDirty = true; }

    float getBrightness()  const { return m_brightness; }
    float getContrast()    const { return m_contrast;   }
    float getSaturation()  const { return m_saturation; }
    float getTemperature() const { return m_temperature;}
    float getTint()        const { return m_tint;       }
    float getExposure()    const { return m_exposure;   }

    // ── RGB 曲线 ──────────────────────────────────────────────────────────

    void setCurves(const RGBCurve& curve);
    const RGBCurve& getCurves() const { return m_curve; }
    void resetCurves();

    // ── HSL ───────────────────────────────────────────────────────────────

    void setHSLAdjustment(const HSLAdjustment& hsl) { m_hsl = hsl; m_shaderDirty = true; }
    const HSLAdjustment& getHSLAdjustment() const   { return m_hsl; }

    // ── 色轮 ──────────────────────────────────────────────────────────────

    void setColorWheels(const ColorWheels& cw) { m_wheels = cw; m_shaderDirty = true; }
    const ColorWheels& getColorWheels() const  { return m_wheels; }

    /** 快捷接口：单独设置阴影色轮 RGB 偏移。 */
    void setColorWheelShadows(float r, float g, float b) {
        m_wheels.shadowLift[0]=r; m_wheels.shadowLift[1]=g;
        m_wheels.shadowLift[2]=b; m_shaderDirty = true;
    }

    // ── LUT ───────────────────────────────────────────────────────────────

    /**
     * 从 .cube 文件加载 LUT（异步，文件 I/O 在调用线程执行）。
     * @return true 表示文件解析成功（GPU 上传在 initialize 后的首次 processFrame 中进行）
     */
    bool loadLutFromCubeFile(const std::string& path);

    /** 直接注入已上传到 GPU 的 LUT 纹理 ID。 */
    void setLutTexture(GLuint texId, int lutSize = 32);

    /** 禁用 LUT（仅保留基础调色）。 */
    void clearLut();

    bool hasLut() const { return m_lutTexId != 0 || !m_lutData.empty(); }

    // ── 重置 ──────────────────────────────────────────────────────────────

    /** 重置所有参数到默认值。 */
    void resetAll();

    // ── Filter 接口 ──────────────────────────────────────────────────────

    std::string getVertexShaderName()   const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "color_grading.frag"; }

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
    std::string getVertexShaderSource()   const override;

private:
    // Basic params
    float m_brightness  = 0.f;
    float m_contrast    = 1.f;
    float m_saturation  = 1.f;
    float m_temperature = 0.f;
    float m_tint        = 0.f;
    float m_exposure    = 0.f;
    float m_highlights  = 0.f;
    float m_shadows     = 0.f;

    RGBCurve      m_curve;
    HSLAdjustment m_hsl;
    ColorWheels   m_wheels;

    // LUT
    GLuint m_lutTexId = 0;
    int    m_lutSize  = 32;
    bool   m_ownLut   = false;   // whether we allocated m_lutTexId
    std::vector<uint8_t> m_lutData; // pending CPU data before GPU upload

    bool m_shaderDirty = false;

    // Curve texture (1D, 256×4 RGBA)
    GLuint m_curveTexId = 0;

    // Uniform locations
    int m_locInputTex = -1;
    int m_locLutTex   = -1;
    int m_locCurveTex = -1;
    int m_locBrightness=-1, m_locContrast=-1, m_locSaturation=-1;
    int m_locTemperature=-1, m_locTint=-1, m_locExposure=-1;
    int m_locHighlights=-1, m_locShadows=-1, m_locUseLut=-1;

    void cacheUniformLocations();
    void uploadCurveTexture();
    void uploadLutTexture();
    bool parseCubeFile(const std::string& path);
};

} // namespace timeline
} // namespace video
} // namespace sdk
