#pragma once
#include "../GLTypes.h"
#include <string>
#include <cstdint>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 文字光栅化接口
 *
 * 将字符串渲染到一块 RGBA 像素缓冲区，由平台层实现：
 *  - Android: 委托 android.graphics.Canvas + Bitmap（JNI 回调）
 *  - iOS:     CoreText + CoreGraphics
 *  - Desktop: 返回占位纹理（mock）
 *
 * 生命周期：由 Compositor 持有，Compositor 不负责 GL 上传，
 * 实现方负责将像素上传为 GL 纹理并返回 Texture 句柄。
 */
class ITextRasterizer {
public:
    struct Style {
        float    x          = 0.5f;    // 中心归一化坐标 [0,1]
        float    y          = 0.85f;   // 中心归一化坐标 [0,1]
        float    fontSizePx = 48.0f;   // 字体像素大小
        uint32_t textColor  = 0xFFFFFFFF; // ARGB
        uint32_t bgColor    = 0x80000000; // ARGB，0=透明背景
        int      alignment  = 1;       // 0=left  1=center  2=right
        float    maxWidthFraction = 0.9f; // 相对于 canvasW 的最大宽度
    };

    virtual ~ITextRasterizer() = default;

    /**
     * @brief 将 text 渲染到 GL 纹理
     * @param text      UTF-8 文本
     * @param style     样式描述
     * @param canvasW   画布宽（匹配输出分辨率）
     * @param canvasH   画布高
     * @return          包含 RGBA 纹理的 Texture，失败时 id==0
     */
    virtual Texture rasterize(const std::string& text,
                              const Style& style,
                              int canvasW, int canvasH) = 0;

    /**
     * @brief 平台层在 GL Context 丢失后调用，通知实现方释放所有纹理句柄
     */
    virtual void onContextLost() {}
};

} // namespace timeline
} // namespace video
} // namespace sdk
