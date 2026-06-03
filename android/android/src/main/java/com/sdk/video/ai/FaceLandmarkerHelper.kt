package com.sdk.video.ai

import android.content.Context
import android.graphics.Bitmap
import android.os.SystemClock
import android.util.Log
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.tasks.core.BaseOptions
import com.google.mediapipe.tasks.core.Delegate
import com.google.mediapipe.tasks.vision.core.RunningMode
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarker
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarkerResult
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * MediaPipe Face Landmarker 封装（LIVE_STREAM 模式）。
 *
 * ## 用法
 * ```kotlin
 * val helper = FaceLandmarkerHelper(context) { landmarks ->
 *     renderEngine.updateFaceLandmarks(landmarks)   // null = 无脸
 * }
 * helper.setup()
 * // 在 CameraX ImageAnalysis.Analyzer 里：
 * helper.detectAsync(bitmap, imageProxy.imageInfo.timestamp / 1_000_000L)
 * // onDestroy：
 * helper.close()
 * ```
 *
 * ## 输出格式
 * `onResult` 回调 FloatArray[478 * 3]，排列为 x0,y0,z0, x1,y1,z1, ...
 * x/y 归一化到 [0,1]（图像宽高），z 为相对深度（负 = 靠近摄像头）。
 * 无人脸时回调 null。
 *
 * ## 线程安全
 * [detectAsync] 可在任意线程调用；[onResult] 在 MediaPipe 内部 worker 线程回调。
 * [setup]/[close] 不需要在 GL 线程，但不可并发调用。
 */
class FaceLandmarkerHelper(
    private val context: Context,
    /** 每帧关键点回调，在 MediaPipe worker 线程调用。null = 本帧无人脸。 */
    private val onResult: (landmarks: FloatArray?) -> Unit,
    private val modelAssetPath: String = MODEL_ASSET_PATH,
    private val maxFaces: Int = 1,
    private val minDetectionConfidence: Float = 0.5f,
    private val minTrackingConfidence: Float = 0.5f,
    private val minPresenceConfidence: Float = 0.5f,
) {
    companion object {
        private const val TAG = "FaceLandmarkerHelper"
        const val MODEL_ASSET_PATH = "models/face_landmarker.task"
    }

    @Volatile private var faceLandmarker: FaceLandmarker? = null
    private val isSetup = AtomicBoolean(false)

    // 串行 executor，确保 setup/close 操作不与推理竞争
    private val bgExecutor: ExecutorService = Executors.newSingleThreadExecutor()

    // ── 初始化 ─────────────────────────────────────────────────────────────

    /**
     * 初始化 FaceLandmarker。异步执行，完成前 [detectAsync] 调用会被安全忽略。
     * 优先 GPU Delegate，失败自动回退 CPU。
     */
    fun setup() {
        bgExecutor.execute { initInternal(useGpu = true) }
    }

    private fun initInternal(useGpu: Boolean) {
        try {
            val baseOptions = BaseOptions.builder()
                .setModelAssetPath(modelAssetPath)
                .setDelegate(if (useGpu) Delegate.GPU else Delegate.CPU)
                .build()

            val options = FaceLandmarker.FaceLandmarkerOptions.builder()
                .setBaseOptions(baseOptions)
                .setRunningMode(RunningMode.LIVE_STREAM)
                .setNumFaces(maxFaces)
                .setMinFaceDetectionConfidence(minDetectionConfidence)
                .setMinTrackingConfidence(minTrackingConfidence)
                .setMinFacePresenceConfidence(minPresenceConfidence)
                // blendshapes / transform matrix 关闭，减少 CPU 开销
                .setOutputFaceBlendshapes(false)
                .setOutputFacialTransformationMatrixes(false)
                .setResultListener(::handleResult)
                .setErrorListener { err ->
                    Log.e(TAG, "MediaPipe error: ${err.message}", err)
                }
                .build()

            faceLandmarker?.close()
            faceLandmarker = FaceLandmarker.createFromOptions(context, options)
            isSetup.set(true)
            Log.i(TAG, "FaceLandmarker ready — GPU=$useGpu, model=$modelAssetPath")

        } catch (e: Exception) {
            if (useGpu) {
                Log.w(TAG, "GPU delegate failed, retrying CPU: ${e.message}")
                initInternal(useGpu = false)
            } else {
                Log.e(TAG, "FaceLandmarker init failed: ${e.message}", e)
            }
        }
    }

    // ── 推理 ──────────────────────────────────────────────────────────────

    /**
     * 提交一帧异步检测。在 CameraX Analyzer（cameraExecutor）线程调用。
     *
     * @param bitmap      当前帧 ARGB_8888 Bitmap，本方法内部负责 recycle
     * @param timestampMs 帧时间戳（毫秒，严格单调递增）。
     *                    推荐用 imageProxy.imageInfo.timestamp / 1_000_000L；
     *                    默认使用 SystemClock.uptimeMillis()。
     */
    fun detectAsync(bitmap: Bitmap, timestampMs: Long = SystemClock.uptimeMillis()) {
        val lm = faceLandmarker
        if (lm == null) {
            bitmap.recycle()
            return
        }
        try {
            val mpImage = BitmapImageBuilder(bitmap).build()
            lm.detectAsync(mpImage, timestampMs)
            // BitmapImageBuilder 不持有 Bitmap 引用，detectAsync 完成后可安全 recycle
            bitmap.recycle()
        } catch (e: Exception) {
            Log.w(TAG, "detectAsync: ${e.message}")
            bitmap.recycle()
        }
    }

    // ── 结果回调 ─────────────────────────────────────────────────────────

    private fun handleResult(result: FaceLandmarkerResult, @Suppress("UNUSED_PARAMETER") input: com.google.mediapipe.framework.image.MPImage) {
        val faces = result.faceLandmarks()
        if (faces.isNullOrEmpty()) {
            onResult(null)
            return
        }
        val lms = faces[0]   // 只取第一张脸
        // 展平为 FloatArray[478*3]：x0,y0,z0, x1,y1,z1, ...
        val out = FloatArray(lms.size * 3)
        lms.forEachIndexed { i, lm ->
            out[i * 3]     = lm.x()
            out[i * 3 + 1] = lm.y()
            out[i * 3 + 2] = lm.z()
        }
        onResult(out)
    }

    // ── 生命周期 ──────────────────────────────────────────────────────────

    fun close() {
        isSetup.set(false)
        bgExecutor.execute {
            try {
                faceLandmarker?.close()
            } catch (e: Exception) {
                Log.w(TAG, "close: ${e.message}")
            } finally {
                faceLandmarker = null
            }
        }
        bgExecutor.shutdown()
    }
}
