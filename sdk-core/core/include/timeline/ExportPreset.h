#pragma once
/**
 * ExportPreset.h
 *
 * 导出格式预设（平台/分辨率/码率/比例/水印）。
 *
 * 预设涵盖主流平台标准：
 *   抖音/TikTok 竖屏 1080p
 *   Instagram Reels 1080p
 *   YouTube Shorts 1080p
 *   快手 720p
 *   微信视频号 1080p
 *   通用 720p / 1080p / 4K 横屏
 *
 * 水印支持：
 *   - 图片水印（PNG，指定路径 + 位置 + 透明度）
 *   - 文字水印（文字内容 + 样式 + 位置）
 *   - 无水印
 *
 * 用法：
 *   auto preset = ExportPreset::douyinVertical1080p();
 *   preset.watermark.imageUri = "/sdcard/logo.png";
 *   preset.watermark.opacity  = 0.5f;
 *   preset.watermark.corner   = WatermarkCorner::BOTTOM_RIGHT;
 *   TimelineExporter exporter;
 *   exporter.exportWithPreset(timeline, preset, outputPath, onProgress);
 */

#include <string>
#include <cstdint>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// 水印位置
// ---------------------------------------------------------------------------
enum class WatermarkCorner {
    TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT, CENTER
};

// ---------------------------------------------------------------------------
// 水印配置
// ---------------------------------------------------------------------------
struct WatermarkConfig {
    bool        enabled    = false;
    std::string imageUri;           ///< 图片水印路径（PNG/WEBP，空=无图片水印）
    std::string text;               ///< 文字水印（空=无文字水印）
    float       opacity    = 0.5f;  ///< [0,1]
    WatermarkCorner corner = WatermarkCorner::BOTTOM_RIGHT;
    float       marginXFrac = 0.03f; ///< 边距（相对于画面宽/高）
    float       marginYFrac = 0.03f;
    float       scaleFrac   = 0.12f; ///< 水印宽度（相对于画面宽度）
    float       textSizePx  = 36.f;
    uint32_t    textColor   = 0xCCFFFFFFu; ///< ARGB
};

// ---------------------------------------------------------------------------
// ExportPreset
// ---------------------------------------------------------------------------
struct ExportPreset {
    std::string name;          ///< 预设名称（显示用）
    std::string platform;      ///< 目标平台（如 "douyin", "instagram", "youtube"）

    // 视频参数
    int         width        = 1080;
    int         height       = 1920;
    int         fps          = 30;
    int         videoBitrate = 8'000'000; ///< bps，0=自动
    std::string videoCodec   = "h264";    ///< "h264" | "h265"
    std::string pixelFormat  = "yuv420p";

    // 音频参数
    int         audioSampleRate = 44100;
    int         audioBitrate    = 192'000;
    std::string audioCodec      = "aac";

    // 比例裁切（输出比例，与 width/height 对应）
    float       aspectRatioW   = 9.f;
    float       aspectRatioH   = 16.f;

    // 时长限制（0 = 不限制）
    int64_t     maxDurationMs  = 0;

    // 水印
    WatermarkConfig watermark;

    // ── 内置预设工厂 ──────────────────────────────────────────────────────

    /** 抖音/TikTok 竖屏 1080×1920 @30fps H.264 8Mbps */
    static ExportPreset douyinVertical1080p() {
        ExportPreset p;
        p.name     = "抖音竖屏 1080p";
        p.platform = "douyin";
        p.width = 1080; p.height = 1920;
        p.fps = 30; p.videoBitrate = 8'000'000;
        p.maxDurationMs = 60'000;
        return p;
    }

    /** Instagram Reels 1080×1920 @30fps */
    static ExportPreset instagramReels() {
        ExportPreset p;
        p.name     = "Instagram Reels";
        p.platform = "instagram";
        p.width = 1080; p.height = 1920;
        p.fps = 30; p.videoBitrate = 6'000'000;
        p.maxDurationMs = 90'000;
        return p;
    }

    /** YouTube Shorts 1080×1920 @60fps */
    static ExportPreset youtubeShorts() {
        ExportPreset p;
        p.name     = "YouTube Shorts";
        p.platform = "youtube";
        p.width = 1080; p.height = 1920;
        p.fps = 60; p.videoBitrate = 10'000'000;
        p.maxDurationMs = 60'000;
        return p;
    }

    /** 微信视频号 1080×1920 @30fps */
    static ExportPreset wechatChannels() {
        ExportPreset p;
        p.name     = "微信视频号";
        p.platform = "wechat";
        p.width = 1080; p.height = 1920;
        p.fps = 30; p.videoBitrate = 6'000'000;
        p.maxDurationMs = 60'000;
        return p;
    }

    /** 快手 720×1280 @30fps */
    static ExportPreset kuaishou720p() {
        ExportPreset p;
        p.name     = "快手 720p";
        p.platform = "kuaishou";
        p.width = 720; p.height = 1280;
        p.fps = 30; p.videoBitrate = 4'000'000;
        p.maxDurationMs = 57'000;
        return p;
    }

    /** 横屏 1080p @30fps */
    static ExportPreset landscape1080p() {
        ExportPreset p;
        p.name     = "横屏 1080p";
        p.platform = "generic";
        p.width = 1920; p.height = 1080;
        p.aspectRatioW = 16.f; p.aspectRatioH = 9.f;
        p.fps = 30; p.videoBitrate = 8'000'000;
        return p;
    }

    /** 横屏 4K @30fps */
    static ExportPreset landscape4K() {
        ExportPreset p;
        p.name     = "横屏 4K";
        p.platform = "generic";
        p.width = 3840; p.height = 2160;
        p.aspectRatioW = 16.f; p.aspectRatioH = 9.f;
        p.fps = 30; p.videoBitrate = 40'000'000;
        p.videoCodec = "h265";
        return p;
    }
};

} // namespace timeline
} // namespace video
} // namespace sdk
