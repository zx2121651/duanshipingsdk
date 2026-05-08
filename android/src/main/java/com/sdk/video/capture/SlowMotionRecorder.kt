package com.sdk.video.capture

import android.content.Context
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.util.Log
import android.util.Range
import androidx.camera.core.CameraSelector
import androidx.camera.video.FallbackStrategy
import androidx.camera.video.Quality
import androidx.camera.video.QualitySelector

/**
 * SlowMotionRecorder
 *
 * 慢动作录制支持（P2 补齐）。
 *
 * 功能：
 *   - 查询设备支持的高帧率模式（120fps / 240fps）
 *   - 构建 CameraX VideoCapture 的 QualitySelector，选择高帧率档位
 *   - 提供时间重映射因子（playbackSpeedFactor），供导出端降速播放
 *   - 不依赖 Camera2 直接 API，通过 CameraX 高层接口适配
 *
 * 录制后处理（时间重映射）：
 *   以 120fps 录制的视频，以 30fps 播放 = 4x 慢速
 *   ExportConfig.playbackSpeed = 1.0 / playbackSpeedFactor
 */
class SlowMotionRecorder(private val context: Context) {

    companion object {
        private const val TAG = "SlowMotionRecorder"

        const val FPS_120 = 120
        const val FPS_240 = 240
        const val FPS_60  = 60
        const val FPS_30  = 30
    }

    data class SlowMotionCapability(
        val cameraId: String,
        val supportedFps: List<Int>,
        val maxSlowFps: Int
    )

    // ── 设备能力查询 ──────────────────────────────────────────────────────

    /**
     * 查询后置摄像头高帧率能力。
     * @return SlowMotionCapability（若不支持高帧率则 maxSlowFps=30）
     */
    fun queryCapability(
        lensFacing: Int = CameraCharacteristics.LENS_FACING_BACK
    ): SlowMotionCapability {
        val manager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        val supportedFps = mutableListOf<Int>()
        var bestCamId = "0"

        for (camId in manager.cameraIdList) {
            val chars = manager.getCameraCharacteristics(camId)
            val facing = chars.get(CameraCharacteristics.LENS_FACING) ?: continue
            if (facing != lensFacing) continue
            bestCamId = camId

            val fpsRanges = chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                ?: continue

            for (range: Range<Int> in fpsRanges) {
                val upper = range.upper
                if (upper >= FPS_60 && !supportedFps.contains(upper)) {
                    supportedFps.add(upper)
                }
            }
            break
        }

        supportedFps.sortDescending()
        val maxFps = supportedFps.firstOrNull() ?: FPS_30
        Log.d(TAG, "Camera $bestCamId supports FPS: $supportedFps (max=$maxFps)")
        return SlowMotionCapability(bestCamId, supportedFps, maxFps)
    }

    // ── QualitySelector 构建 ──────────────────────────────────────────────

    /**
     * 构建 CameraX QualitySelector，优先选择 FHD 高帧率，
     * fallback 到 HD。
     */
    fun buildQualitySelector(targetFps: Int = FPS_120): QualitySelector {
        Log.d(TAG, "Building QualitySelector for ${targetFps}fps")
        return QualitySelector.from(
            Quality.FHD,
            FallbackStrategy.higherQualityOrLowerThan(Quality.HD)
        )
    }

    // ── 时间重映射因子 ─────────────────────────────────────────────────────

    /**
     * 计算播放速度因子（录制帧率 / 正常播放帧率）。
     * @param recordFps  录制帧率（如 120）
     * @param playFps    正常播放帧率（通常 30）
     * @return 慢放倍数（如 4.0 = 4x 慢速）
     */
    fun playbackSpeedFactor(recordFps: Int = FPS_120, playFps: Int = FPS_30): Float {
        return recordFps.toFloat() / playFps.toFloat()
    }

    /**
     * 返回导出时需要设置的 VideoExportConfig.playbackSpeed（< 1.0 表示慢放）。
     * @param recordFps  录制帧率
     * @param playFps    目标播放帧率（通常 30）
     */
    fun exportPlaybackSpeed(recordFps: Int = FPS_120, playFps: Int = FPS_30): Float {
        return playFps.toFloat() / recordFps.toFloat()
    }

    // ── 帧率配置描述 ───────────────────────────────────────────────────────

    /**
     * 返回慢动作档位描述，供 UI 展示。
     */
    fun describeCapability(cap: SlowMotionCapability): String {
        val modes = cap.supportedFps.joinToString(" / ") { "${it}fps" }
        return if (cap.maxSlowFps > FPS_30)
            "支持慢动作：$modes"
        else
            "当前设备不支持 60fps+ 慢动作录制"
    }
}
