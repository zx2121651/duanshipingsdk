#pragma once
/**
 * VideoTemplate.h
 *
 * 模板一键成片 — 数据模型定义。
 *
 * JSON Schema（模板文件 assets/templates/*.json）：
 * {
 *   "id":          "romantic_15s",       // 唯一 ID
 *   "name":        "浪漫爱情",           // 显示名称
 *   "version":     1,                    // schema 版本
 *   "duration_ms": 15000,                // 总时长（毫秒）
 *   "fps":         30,
 *   "width":       1080,
 *   "height":      1920,
 *   "slots": [
 *     {
 *       "id":            "v0",
 *       "type":          "video",        // "video" | "image"
 *       "duration_ms":   5000,
 *       "transition":    "crossfade",    // 入场转场名（见 TransitionType）
 *       "trans_dur_ms":  500,
 *       "effect":        "beauty_soft",  // 可选：特效包 ID
 *       "fit":           "crop_center"   // "crop_center" | "letterbox" | "stretch"
 *     },
 *     ...
 *   ],
 *   "subtitles": [
 *     {
 *       "id":         "sub_0",
 *       "text":       "美好时光",
 *       "start_ms":   0,
 *       "end_ms":     2000,
 *       "font_size":  56,
 *       "color":      4294967295,       // ARGB uint32 (0xFFFFFFFF = 白色)
 *       "y":          0.85              // 归一化纵坐标
 *     }
 *   ],
 *   "audio": {
 *     "type":   "music",               // "music" | "none"
 *     "path":   "assets/music/romantic.mp3",
 *     "loop":   true,
 *     "volume": 0.8
 *   },
 *   "lut": {
 *     "path":      "assets/luts/warm.png",
 *     "intensity": 0.7
 *   }
 * }
 */

#include <string>
#include <vector>
#include <cstdint>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// 素材填充模式
// ---------------------------------------------------------------------------
enum class SlotFitMode {
    CROP_CENTER,  ///< 等比缩放 + 居中裁切（默认）
    LETTERBOX,    ///< 等比缩放 + 黑边
    STRETCH       ///< 拉伸填满
};

inline SlotFitMode slotFitModeFromString(const std::string& s) {
    if (s == "letterbox") return SlotFitMode::LETTERBOX;
    if (s == "stretch")   return SlotFitMode::STRETCH;
    return SlotFitMode::CROP_CENTER;
}

// ---------------------------------------------------------------------------
// 单个素材槽（用户需填入视频/图片路径）
// ---------------------------------------------------------------------------
struct TemplateSlot {
    std::string  id;
    std::string  type         = "video";       ///< "video" | "image"
    int64_t      durationMs   = 3000;
    std::string  transition   = "crossfade";   ///< 入场转场名
    int64_t      transDurMs   = 500;
    std::string  effectId;                     ///< 可选特效包 ID
    SlotFitMode  fitMode      = SlotFitMode::CROP_CENTER;

    // 用户填充字段（调用 TemplateEngine::apply() 时赋值）
    std::string  filledPath;                   ///< 实际素材路径
};

// ---------------------------------------------------------------------------
// 模板内置字幕条目
// ---------------------------------------------------------------------------
struct TemplateSubtitle {
    std::string id;
    std::string text;
    int64_t     startMs    = 0;
    int64_t     endMs      = 2000;
    float       fontSizePx = 48.f;
    uint32_t    textColor  = 0xFFFFFFFFu;  ///< ARGB
    float       x          = 0.5f;
    float       y          = 0.85f;
    int         alignment  = 1;            ///< 0=left,1=center,2=right
};

// ---------------------------------------------------------------------------
// 模板音频配置
// ---------------------------------------------------------------------------
struct TemplateAudio {
    std::string type    = "none";  ///< "music" | "none"
    std::string path;
    bool        loop    = true;
    float       volume  = 0.8f;
};

// ---------------------------------------------------------------------------
// 模板 LUT 滤镜
// ---------------------------------------------------------------------------
struct TemplateLut {
    std::string path;
    float       intensity = 0.7f;
};

// ---------------------------------------------------------------------------
// VideoTemplate — 完整模板描述
// ---------------------------------------------------------------------------
struct VideoTemplate {
    std::string  id;
    std::string  name;
    int          version    = 1;
    int64_t      durationMs = 15000;
    int          fps        = 30;
    int          width      = 1080;
    int          height     = 1920;

    std::vector<TemplateSlot>     slots;
    std::vector<TemplateSubtitle> subtitles;
    TemplateAudio                 audio;
    TemplateLut                   lut;

    /** 模板需要用户填充的素材槽数量 */
    int slotCount() const { return static_cast<int>(slots.size()); }

    /** 校验：所有 slot 都已填充了素材路径 */
    bool allSlotsFilled() const {
        for (const auto& s : slots) {
            if (s.filledPath.empty()) return false;
        }
        return true;
    }
};

} // namespace timeline
} // namespace video
} // namespace sdk
