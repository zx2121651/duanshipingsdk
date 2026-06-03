package com.sdk.video.capture

import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.hardware.camera2.params.StreamConfigurationMap
import android.util.Log
import android.util.Size
import androidx.camera.core.CameraSelector
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * 通过 Camera2 API 枚举当前设备摄像头真实支持的预览输出尺寸。
 *
 * CameraX 的 ResolutionSelector 基于 Camera2 的 StreamConfigurationMap，
 * 但只暴露"请求-协商"接口，不直接枚举支持列表。本工具通过 CameraManager
 * 直接读取设备能力，确保：
 * 1. UI 只展示设备真正支持的尺寸
 * 2. 预设映射到设备实际尺寸（避免 fallback 到同一尺寸）
 * 3. SurfaceTexture 缓冲区尺寸与 CameraX 输出一致
 */
object CameraResolutionHelper {

    private const val TAG = "CameraResolutionHelper"

    /**
     * 查询指定摄像头的 SurfaceTexture 输出支持列表。
     *
     * @param context   Android Context
     * @param useFront  true=前置，false=后置
     * @return 支持的尺寸列表（按像素数降序排列）
     */
    suspend fun getSupportedPreviewSizes(
        context: Context,
        useFront: Boolean
    ): List<Size> = withContext(Dispatchers.IO) {
        val manager = context.getSystemService(Context.CAMERA_SERVICE) as? CameraManager
            ?: return@withContext emptyList()

        val cameraId = findCameraId(manager, useFront) ?: return@withContext emptyList()

        val characteristics = manager.getCameraCharacteristics(cameraId)
        val configMap = characteristics.get(
            CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP
        ) ?: return@withContext emptyList()

        val sizes = configMap.getOutputSizes(SurfaceTexture::class.java)
            ?.distinct()
            ?.sortedByDescending { it.width * it.height }
            ?: emptyList()

        Log.d(TAG, "Camera ${if (useFront) "front" else "back"}: ${sizes.size} supported sizes")
        sizes.take(20).forEach { s -> Log.d(TAG, "  ${s.width}x${s.height}") }
        sizes
    }

    /**
     * 从支持列表中选取与目标分辨率比例最接近的尺寸。
     *
     * 优先完全匹配，其次宽高比匹配，再次最接近像素数。
     *
     * @param supported  设备支持列表
     * @param target     目标尺寸（通常为横屏：1920x1080 等）
     * @return 最接近的尺寸，若无匹配则返回 null
     */
    fun pickBestSize(supported: List<Size>, target: Size): Size? {
        if (supported.isEmpty()) return null

        // 1. 精确匹配优先
        supported.firstOrNull { it.width == target.width && it.height == target.height }
            ?.let { return it }

        val targetRatio = target.width.toFloat() / target.height

        // 2. 相同宽高比中选最接近像素数的
        val ratioMatches = supported.filter {
            val r = it.width.toFloat() / it.height
            kotlin.math.abs(r - targetRatio) < 0.01f
        }

        if (ratioMatches.isNotEmpty()) {
            return ratioMatches.minByOrNull {
                kotlin.math.abs(it.width * it.height - target.width * target.height)
            }
        }

        // 3. 全局最接近
        return supported.minByOrNull {
            kotlin.math.abs(it.width.toFloat() / it.height - targetRatio)
        }
    }

    /**
     * 将宽高比锁定到 16:9 范围内，并选取不超过 maxPixels 的最大尺寸。
     */
    fun pickBest16x9(supported: List<Size>, maxPixels: Int): Size? {
        val targetRatio = 16f / 9f
        return supported
            .filter {
                val ratio = it.width.toFloat() / it.height
                kotlin.math.abs(ratio - targetRatio) < 0.05f &&
                it.width * it.height <= maxPixels
            }
            .maxByOrNull { it.width * it.height }
    }

    /** 根据前/后置需求找到对应的 Camera2 cameraId */
    private fun findCameraId(manager: CameraManager, useFront: Boolean): String? {
        return manager.cameraIdList.firstOrNull { id ->
            val chars = manager.getCameraCharacteristics(id)
            val facing = chars.get(CameraCharacteristics.LENS_FACING)
            if (useFront) facing == CameraCharacteristics.LENS_FACING_FRONT
            else          facing == CameraCharacteristics.LENS_FACING_BACK
        }
    }
}
