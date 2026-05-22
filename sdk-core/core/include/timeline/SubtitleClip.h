#pragma once
#include "Clip.h"
#include "ITextRasterizer.h"
#include <string>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 字幕片段（Subtitle / Text Overlay Clip）
 *
 * 继承自 Clip（复用时间轴定位、关键帧、转场属性），
 * 额外携带文字内容和排版样式。
 *
 * 使用方式：
 *   auto sub = std::make_shared<SubtitleClip>("sub_0", "Hello World");
 *   sub->style.fontSizePx = 56;
 *   sub->style.y = 0.9f;
 *   track->addClip(sub);                // TrackType::SUBTITLE 轨道
 */
class SubtitleClip : public Clip {
public:
    SubtitleClip(const std::string& id, const std::string& text)
        : Clip(id, "", Clip::MediaType::TEXT), m_text(text)
    {}

    const std::string& getText() const { return m_text; }
    void setText(const std::string& text) { m_text = text; }

    ITextRasterizer::Style style;   // 公开可直接写

private:
    std::string m_text;
};

/**
 * @brief 贴纸片段（Sticker / Image Overlay Clip）
 *
 * 源文件可以是静态图 (PNG/JPEG) 或 GIF 帧序列目录。
 * 当前实现只支持静态图；GIF 通过关键帧路径切换实现帧动画。
 */
class StickerClip : public Clip {
public:
    /**
     * @param id          唯一标识
     * @param imagePath   静态图资源路径（PNG / JPEG / WEBP）
     */
    StickerClip(const std::string& id, const std::string& imagePath)
        : Clip(id, imagePath, Clip::MediaType::STICKER)
    {}

    // 归一化中心点 [0,1]
    float centerX = 0.5f;
    float centerY = 0.5f;
    // 相对于画布宽度的缩放比例
    float stickerScale = 0.3f;
    // 旋转角（弧度）
    float stickerRotation = 0.0f;
};

using SubtitleClipPtr = std::shared_ptr<SubtitleClip>;
using StickerClipPtr  = std::shared_ptr<StickerClip>;

} // namespace timeline
} // namespace video
} // namespace sdk
