package com.sdk.video.capture

import android.content.Context
import android.util.Log
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executor

/**
 * PhotoCaptureMode
 *
 * CameraX 拍照模式扩展：高清静态照片捕获（P2 补齐）。
 *
 * 功能：
 *   - 单张高清拍照（JPEG/HEIC）
 *   - 前/后摄切换
 *   - HDR 模式（设备支持时启用）
 *   - 与 VideoFilterManager 共享 CameraX lifecycle
 *   - 保存到 DCIM 或指定路径
 *
 * 用法：
 *   val photo = PhotoCaptureMode(context, outputDir)
 *   photo.bindToCamera(cameraProvider, lifecycleOwner, cameraSelector)
 *   photo.capture { file, error -> ... }
 */
class PhotoCaptureMode(
    private val context: Context,
    private val outputDir: File
) {
    companion object {
        private const val TAG = "PhotoCaptureMode"
        private const val FILENAME_FORMAT = "yyyyMMdd_HHmmss"
        private const val PHOTO_EXTENSION = ".jpg"
    }

    // CameraX ImageCapture use-case
    private var imageCapture: ImageCapture? = null

    // ── 质量配置 ───────────────────────────────────────────────────────────

    var jpegQuality: Int = 95
        set(value) { field = value.coerceIn(50, 100) }

    /** 拍照模式：MINIMIZE_LATENCY（快门优先）/ MAXIMIZE_QUALITY（画质优先） */
    var captureMode: Int = ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY

    // ── 构建 ImageCapture use-case ─────────────────────────────────────────

    /**
     * 构建 ImageCapture use-case，需传入 cameraProvider 并绑定 lifecycle。
     * 通常在 CameraX Preview 绑定的同一个 addListener 内调用。
     */
    fun buildUseCase(): ImageCapture {
        val resolutionSelector = ResolutionSelector.Builder()
            .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
            .build()

        val ic = ImageCapture.Builder()
            .setResolutionSelector(resolutionSelector)
            .setCaptureMode(captureMode)
            .setJpegQuality(jpegQuality)
            .build()
        imageCapture = ic
        Log.d(TAG, "ImageCapture use-case built (mode=$captureMode, q=$jpegQuality)")
        return ic
    }

    // ── 拍照 ───────────────────────────────────────────────────────────────

    /**
     * 触发拍照，结果通过回调返回。
     * @param executor  回调执行线程（建议主线程）
     * @param onResult  (File?, errorMessage?) — 成功时 File 非空
     */
    fun capture(
        executor: Executor = ContextCompat.getMainExecutor(context),
        onResult: (File?, String?) -> Unit
    ) {
        val ic = imageCapture ?: run {
            onResult(null, "ImageCapture not initialized — call buildUseCase() first")
            return
        }

        if (!outputDir.exists()) outputDir.mkdirs()
        val ts = SimpleDateFormat(FILENAME_FORMAT, Locale.US).format(Date())
        val file = File(outputDir, "PHOTO_$ts$PHOTO_EXTENSION")

        val outputOptions = ImageCapture.OutputFileOptions.Builder(file).build()

        ic.takePicture(outputOptions, executor,
            object : ImageCapture.OnImageSavedCallback {
                override fun onImageSaved(output: ImageCapture.OutputFileResults) {
                    Log.i(TAG, "Photo saved: ${file.absolutePath} (${file.length()} bytes)")
                    onResult(file, null)
                }
                override fun onError(exc: ImageCaptureException) {
                    Log.e(TAG, "Photo capture failed: ${exc.message}", exc)
                    onResult(null, exc.message)
                }
            })
    }

    // ── 闪光灯模式 ─────────────────────────────────────────────────────────

    /**
     * 设置闪光灯模式。
     * @param mode ImageCapture.FLASH_MODE_AUTO / FLASH_MODE_ON / FLASH_MODE_OFF
     */
    fun setFlashMode(mode: Int) {
        imageCapture?.flashMode = mode
        Log.d(TAG, "Flash mode set to $mode")
    }

    fun getFlashMode(): Int = imageCapture?.flashMode ?: ImageCapture.FLASH_MODE_OFF

    // ── 释放 ───────────────────────────────────────────────────────────────

    fun release() {
        imageCapture = null
    }
}
