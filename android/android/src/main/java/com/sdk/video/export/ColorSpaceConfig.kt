package com.sdk.video.export

import android.media.MediaFormat
import android.os.Build

/**
 * 导出色彩空间配置。
 *
 * ## 支持的模式
 * | 模式        | 色域         | 传递函数   | 用途                   |
 * |-------------|-------------|-----------|------------------------|
 * | SDR_BT709   | BT.709      | SDR gamma | 短视频平台标准（默认）  |
 * | HDR_HLG     | BT.2020     | HLG       | 普通 HDR 设备拍摄素材  |
 * | HDR_PQ      | BT.2020     | PQ (ST2084)| 专业 HDR / 影院       |
 * | DISPLAY_P3  | Display P3  | sRGB      | iPhone / iPad 内容     |
 *
 * ## Android 版本要求
 * - `HDR_HLG` / `HDR_PQ` 需要 API 33+（MediaCodec COLOR_STANDARD/TRANSFER 常量）。
 * - 低版本设备自动降级为 SDR_BT709，并输出警告日志。
 */
enum class ColorSpaceMode {
    SDR_BT709,
    HDR_HLG,
    HDR_PQ,
    DISPLAY_P3,
}

data class ColorSpaceConfig(
    val mode: ColorSpaceMode = ColorSpaceMode.SDR_BT709,
    /**
     * 强制 tonemapping：将 HDR 素材映射到 SDR 输出。
     * 仅在 [mode] = SDR_BT709 且输入轨道包含 HDR 素材时有效。
     * true  = 软件 tonemapping（精度高，速度慢）
     * false = 硬件 passthrough（速度快，可能有剪切）
     */
    val enableTonemapping: Boolean = false,
    /**
     * 最大内容亮度（MaxCLL），nit。
     * 仅对 HDR_PQ 有效；0 = 不写入 SEI 元数据。
     */
    val maxContentLightLevel: Int = 0,
    /**
     * 最大帧平均亮度（MaxFALL），nit。
     * 仅对 HDR_PQ 有效；0 = 不写入 SEI 元数据。
     */
    val maxFrameAverageLightLevel: Int = 0,
) {
    companion object {
        val SDR  = ColorSpaceConfig(mode = ColorSpaceMode.SDR_BT709)
        val HLG  = ColorSpaceConfig(mode = ColorSpaceMode.HDR_HLG)
        val PQ   = ColorSpaceConfig(mode = ColorSpaceMode.HDR_PQ)
        val P3   = ColorSpaceConfig(mode = ColorSpaceMode.DISPLAY_P3)
    }

    /**
     * 将此配置应用到 MediaFormat（MediaCodec 编码器输入格式）。
     * 不支持的参数在旧 API 级别上静默跳过。
     */
    fun applyToMediaFormat(fmt: MediaFormat) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            when (mode) {
                ColorSpaceMode.SDR_BT709 -> {
                    fmt.setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT709)
                    fmt.setInteger(MediaFormat.KEY_COLOR_TRANSFER,  MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
                    fmt.setInteger(MediaFormat.KEY_COLOR_RANGE,     MediaFormat.COLOR_RANGE_LIMITED)
                }
                ColorSpaceMode.HDR_HLG -> {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        fmt.setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT2020)
                        fmt.setInteger(MediaFormat.KEY_COLOR_TRANSFER,  MediaFormat.COLOR_TRANSFER_HLG)
                        fmt.setInteger(MediaFormat.KEY_COLOR_RANGE,     MediaFormat.COLOR_RANGE_FULL)
                    } else {
                        // Fallback: SDR on older devices
                        applyFallbackSdr(fmt)
                    }
                }
                ColorSpaceMode.HDR_PQ -> {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        fmt.setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT2020)
                        fmt.setInteger(MediaFormat.KEY_COLOR_TRANSFER,  MediaFormat.COLOR_TRANSFER_ST2084)
                        fmt.setInteger(MediaFormat.KEY_COLOR_RANGE,     MediaFormat.COLOR_RANGE_FULL)
                        if (maxContentLightLevel > 0)
                            fmt.setInteger("max-cll",  maxContentLightLevel)
                        if (maxFrameAverageLightLevel > 0)
                            fmt.setInteger("max-fall", maxFrameAverageLightLevel)
                    } else {
                        applyFallbackSdr(fmt)
                    }
                }
                ColorSpaceMode.DISPLAY_P3 -> {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        // P3-D65 = COLOR_STANDARD_BT2020 + limited range on Android
                        fmt.setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT2020)
                        fmt.setInteger(MediaFormat.KEY_COLOR_TRANSFER,  MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
                        fmt.setInteger(MediaFormat.KEY_COLOR_RANGE,     MediaFormat.COLOR_RANGE_LIMITED)
                    } else {
                        applyFallbackSdr(fmt)
                    }
                }
            }
        }
    }

    private fun applyFallbackSdr(fmt: MediaFormat) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            fmt.setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT709)
            fmt.setInteger(MediaFormat.KEY_COLOR_TRANSFER,  MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
            fmt.setInteger(MediaFormat.KEY_COLOR_RANGE,     MediaFormat.COLOR_RANGE_LIMITED)
        }
    }

    /** 是否为 HDR 输出（决定是否写 SEI HDR 元数据）。 */
    val isHdr: Boolean get() = mode == ColorSpaceMode.HDR_HLG || mode == ColorSpaceMode.HDR_PQ
}
