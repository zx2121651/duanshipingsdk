#pragma once
/**
 * ChromaKeyFilter.h
 *
 * 实时绿幕/色度键抠图滤镜（HSV 空间颜色范围 + 边缘羽化）。
 *
 * 工作原理：
 *   1. 将像素从 RGB 转换到 HSV 颜色空间
 *   2. 检测 [hueMin, hueMax] × [satMin,1] × [valMin,1] 范围内的像素为背景
 *   3. 计算软边缘 alpha（smoothstep）消除锯齿
 *   4. 将背景像素替换为 replacementTexture 或透明 (TRANSPARENT 模式)
 *
 * 用法：
 *   auto chroma = std::make_shared<ChromaKeyFilter>();
 *   chroma->setKeyColor(0.33f, 0.1f, 0.08f); // 绿色: hue≈120°→0.33 归一化
 *   chroma->setEdgeSoftness(0.08f);
 *   chroma->setMode(ChromaKeyFilter::Mode::TRANSPARENT);
 *   chroma->processFrame(inputTex, outputFb);
 *
 * setKeyColor 参数均为归一化 [0,1]：
 *   hueCenter  HSV 色相中心 [0,1]，绿 ≈ 0.333, 蓝 ≈ 0.667
 *   hueTol     色相容差（单侧），建议 0.08–0.15
 *   satMin     饱和度下限，低于此值视为非背景，建议 0.2–0.4
 */

#include "../Filter.h"
#include "../FrameBufferPool.h"
#include "../GLTypes.h"
#include <memory>

namespace sdk {
namespace video {

class ChromaKeyFilter : public Filter {
public:
    enum class Mode : int {
        TRANSPARENT  = 0,  ///< 背景变透明（alpha=0），前景保留
        REPLACE_BG   = 1,  ///< 背景替换为纯色（bgColor 参数）
        IMAGE_BG     = 2,  ///< 背景替换为图像纹理（bgImageTexId）
    };

    explicit ChromaKeyFilter(FrameBufferPool* pool = nullptr);
    ~ChromaKeyFilter() override;

    // ── 键控参数 ──────────────────────────────────────────────────────────

    /**
     * 设置键控颜色（HSV 归一化）。
     * @param hueCenter  色相中心 [0,1]，绿色 ≈ 0.333
     * @param hueTol     色相容差（单侧）[0,0.5]，默认 0.10
     * @param satMin     饱和度下限 [0,1]，默认 0.25
     */
    void setKeyColor(float hueCenter, float hueTol = 0.10f, float satMin = 0.25f);

    /** 便捷：从 ARGB uint32 颜色设置键控颜色（自动转 HSV）。 */
    void setKeyColorFromARGB(uint32_t argb);

    /**
     * 边缘软化半径 [0,1]，值越大羽化越宽，默认 0.06。
     */
    void setEdgeSoftness(float s) { m_edgeSoftness = s; }
    float getEdgeSoftness() const { return m_edgeSoftness; }

    /** 前景溢色抑制强度 [0,1]，消除绿幕在前景边缘的颜色投射，默认 0.3。 */
    void setSpillSuppression(float v) { m_spillSuppression = v; }

    /** 替换背景颜色（REPLACE_BG 模式，ARGB uint32）。 */
    void setBgColor(uint32_t argb) { m_bgColor = argb; }

    /** 替换背景纹理（IMAGE_BG 模式）。 */
    void setBgImageTexture(GLuint texId) { m_bgImageTexId = texId; }

    /** 设置合成模式。 */
    void setMode(Mode mode) { m_mode = mode; }
    Mode getMode() const   { return m_mode; }

    // ── Filter 接口 ──────────────────────────────────────────────────────

    std::string getVertexShaderName()   const override { return "chroma_key.vert"; }
    std::string getFragmentShaderName() const override { return "chroma_key.frag"; }

    Result initialize() override;
    void   onProgramRecompiled() override;
    ResultPayload<Texture> processFrame(const Texture& inputTexture,
                                        FrameBufferPtr outputFb) override;

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
    std::string getVertexShaderSource()   const override;

private:
    FrameBufferPool* m_pool;

    // 键控参数
    float    m_hueCenter       = 0.333f;  // 绿色
    float    m_hueTol          = 0.10f;
    float    m_satMin          = 0.25f;
    float    m_edgeSoftness    = 0.06f;
    float    m_spillSuppression= 0.30f;
    uint32_t m_bgColor         = 0xFF000000u;
    GLuint   m_bgImageTexId    = 0;
    Mode     m_mode            = Mode::TRANSPARENT;

    // Uniform 位置
    GLint    m_locInput        = -1;
    GLint    m_locBgImage      = -1;
    GLint    m_locHueCenter    = -1;
    GLint    m_locHueTol       = -1;
    GLint    m_locSatMin       = -1;
    GLint    m_locEdgeSoft     = -1;
    GLint    m_locSpill        = -1;
    GLint    m_locBgColor      = -1;
    GLint    m_locMode         = -1;

    void cacheUniformLocations();

    // RGB→HSV helper (CPU side for setKeyColorFromARGB)
    static void rgbToHsv(float r, float g, float b,
                         float& h, float& s, float& v);
};

} // namespace video
} // namespace sdk
