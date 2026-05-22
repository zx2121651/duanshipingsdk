#pragma once
/**
 * SplitScreenCompositor.h
 *
 * 分屏/多视角布局合成器（P3 补齐）。
 *
 * 功能：
 *   - 支持 1/2/3/4/6/9 宫格布局
 *   - 每格接受独立纹理输入（VideoFrame / Texture）
 *   - 支持等比格子或自定义格子权重
 *   - 格间距、圆角、边框颜色可配置
 *   - 单 draw-call OpenGL ES 3.0 合成（quad per cell）
 *
 * 用法：
 *   SplitScreenCompositor compositor;
 *   compositor.setLayout(SplitScreenCompositor::Layout::GRID_2x2);
 *   compositor.setGapPx(4);
 *   compositor.setSlot(0, tex0);
 *   compositor.setSlot(1, tex1);
 *   compositor.setSlot(2, tex2);
 *   compositor.setSlot(3, tex3);
 *   compositor.render(outputFbo, outputW, outputH);
 */

#include "../Filter.h"
#include "../GLTypes.h"
#include <array>
#include <vector>
#include <cstdint>

namespace sdk {
namespace video {
namespace timeline {

class SplitScreenCompositor {
public:
    enum class Layout {
        SINGLE   = 1,   ///< 全屏单窗口
        SPLIT_H  = 2,   ///< 左右分屏
        SPLIT_V  = 2,   ///< 上下分屏（alias）
        GRID_2x2 = 4,   ///< 田字四格
        GRID_3x2 = 6,   ///< 六宫格
        GRID_3x3 = 9,   ///< 九宫格
        CUSTOM   = 0,   ///< 自定义格子
    };

    // 单个格子描述（归一化坐标 [0,1]）
    struct Cell {
        float x = 0.f, y = 0.f;  ///< 左上角
        float w = 1.f, h = 1.f;  ///< 宽高
        GLuint texId = 0;         ///< 输入纹理（0=填充背景色）
        bool   flipH = false;
        bool   flipV = false;
    };

    SplitScreenCompositor();
    ~SplitScreenCompositor();

    // ── 配置 ──────────────────────────────────────────────────────────────

    void setLayout(Layout layout);
    Layout getLayout() const { return m_layout; }

    /** 格间距（像素，在最终输出尺寸下）。 */
    void setGapPx(int px) { m_gapPx = px; }

    /** 背景颜色（ARGB，填充无纹理格子）。 */
    void setBackgroundColor(uint32_t argb) { m_bgColor = argb; }

    /** 边框宽度（像素）和颜色。 */
    void setBorder(int widthPx, uint32_t argb) { m_borderPx=widthPx; m_borderColor=argb; }

    // ── 纹理输入 ──────────────────────────────────────────────────────────

    /** 设置第 slot 个格子的纹理。 */
    void setSlot(int slot, GLuint texId);

    /** 完全自定义格子布局（CUSTOM 模式）。 */
    void setCells(const std::vector<Cell>& cells);

    int slotCount() const { return (int)m_cells.size(); }

    // ── GL 初始化 ─────────────────────────────────────────────────────────

    /** 必须在 GL 上下文有效时调用一次。 */
    bool initialize();
    void release();

    // ── 渲染 ──────────────────────────────────────────────────────────────

    /**
     * 合成所有格子到 outputFbo。
     * @param outputFboId   目标 FBO（0=默认帧缓冲）
     * @param outputW/H     输出分辨率
     */
    void render(GLuint outputFboId, int outputW, int outputH);

    /** 便捷重载：直接渲染到 FrameBuffer。 */
    void render(FrameBufferPtr outputFb);

private:
    Layout   m_layout     = Layout::SPLIT_H;
    int      m_gapPx      = 2;
    uint32_t m_bgColor    = 0xFF000000u;
    int      m_borderPx   = 0;
    uint32_t m_borderColor= 0xFFFFFFFFu;

    std::vector<Cell> m_cells;

    // GL resources
    bool   m_initialized = false;
    GLuint m_program  = 0;
    GLuint m_vao      = 0;
    GLuint m_vbo      = 0;

    // Uniform locations
    int m_locInputTex = -1;
    int m_locFlipH    = -1;
    int m_locFlipV    = -1;

    void buildLayout(Layout layout, int outputW, int outputH);
    void drawCell(const Cell& cell, int outputW, int outputH);
    static GLuint compileProgram(const char* vert, const char* frag);
};

} // namespace timeline
} // namespace video
} // namespace sdk
