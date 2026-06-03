@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.ai

import android.content.Context
import android.util.Log
import com.sdk.video.RenderEngine
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * AiFilterController — AI 滤镜统一控制器
 *
 * 封装"模型解压 → 路径传递 → Native 加载"全链路，隐藏 [ModelAssetManager] 和
 * [RenderEngine] AI API 的细节。
 *
 * ## 设计约束
 * - 模型解压在 IO 线程（通过 [ModelAssetManager.extractModel]），加载由调用方
 *   保证在 GL 线程执行（`renderEngine.loadXxx` 内部无 GL 调用，可在任意线程调用）。
 * - 所有回调均在 [scope] 内运行（默认 `Dispatchers.Default + SupervisorJob`）。
 *
 * ## 典型用法（ViewModel 中）
 * ```kotlin
 * val aiCtrl = AiFilterController(context, renderEngine)
 * aiCtrl.enableFaceLandmarks { loaded ->
 *     if (loaded) renderEngine.setFaceReshape(eyeScale = 0.3f)
 * }
 * aiCtrl.enableHairColoring {
 *     if (it) renderEngine.setHairColor(0.3f, 0.15f, 0.05f)
 * }
 * ```
 */
class AiFilterController(
    private val context: Context,
    private val renderEngine: RenderEngine,
    private val delegatePreference: TfliteDelegateStrategy.Preference = TfliteDelegateStrategy.Preference.AUTO,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Default + SupervisorJob())
) {
    companion object {
        private const val TAG = "AiFilterController"

        const val ASSET_FACE_LANDMARK   = "models/face_landmark_stub.tflite"
        const val ASSET_HAIR_MODEL      = "models/selfie_segmentation_stub.tflite"
        const val ASSET_HAND_MODEL      = "models/hand_landmark_stub.tflite"
        const val ASSET_SEG_MODEL       = "models/portrait_segmentation_stub.tflite"
    }

    private val assetManager = ModelAssetManager(context)
    private val delegateStrategy = TfliteDelegateStrategy(context)

    /** Current load state, observable by UI */
    enum class AiModelState { IDLE, LOADING, READY, STUB, ERROR }

    @Volatile var faceLandmarkState: AiModelState = AiModelState.IDLE
        private set
    @Volatile var hairModelState: AiModelState = AiModelState.IDLE
        private set
    @Volatile var handLandmarkState: AiModelState = AiModelState.IDLE
        private set
    @Volatile var segmentationState: AiModelState = AiModelState.IDLE
        private set

    /**
     * MediaPipe FaceLandmarker 实例，setup() 后可用。
     * 在 CameraX ImageAnalysis.Analyzer 中调用 [FaceLandmarkerHelper.detectAsync]。
     * 每帧结果通过 [RenderEngine.updateFaceLandmarks] 直接注入渲染管线。
     */
    var faceLandmarkerHelper: FaceLandmarkerHelper? = null
        private set

    // ── Face Landmark ─────────────────────────────────────────────────────────

    /**
     * 启用人脸关键点检测。
     *
     * 优先走 MediaPipe Tasks API（LIVE_STREAM 模式，GPU delegate）；
     * 若 assets 中缺少 face_landmarker.task，则回退到旧的 TFLite 路径。
     *
     * 成功后 [faceLandmarkerHelper] 可用，在相机 Analyzer 中调用 detectAsync()。
     *
     * @param onResult  true = 检测器就绪，false = 加载失败或 stub
     */
    fun enableFaceLandmarks(onResult: ((Boolean) -> Unit)? = null) {
        faceLandmarkState = AiModelState.LOADING
        scope.launch(Dispatchers.IO) {
            val modelAvailable = try {
                context.assets.open(FaceLandmarkerHelper.MODEL_ASSET_PATH).close()
                true
            } catch (_: Exception) { false }

            if (modelAvailable) {
                // ── MediaPipe 路径 ──────────────────────────────────────────────
                // helper.setup() 是异步的（内部 bgExecutor），这里 READY 表示
                // "helper 已创建并开始初始化"，detectAsync 在 setup 完成前会
                // 被 helper 内部安全忽略（faceLandmarker == null 时直接 recycle bitmap）。
                val helper = FaceLandmarkerHelper(
                    context = context,
                    onResult = { landmarks ->
                        renderEngine.updateFaceLandmarks(landmarks)
                    }
                )
                helper.setup()
                faceLandmarkerHelper = helper
                faceLandmarkState = AiModelState.READY
                Log.i(TAG, "FaceLandmarker initializing via MediaPipe Tasks API")
                onResult?.invoke(true)
            } else {
                // ── 旧 TFLite 兜底路径 ─────────────────────────────────────────
                Log.w(TAG, "${FaceLandmarkerHelper.MODEL_ASSET_PATH} not found, falling back to TFLite stub")
                when (val r = assetManager.extractModel(ASSET_FACE_LANDMARK)) {
                    is ModelAssetManager.ModelLoadResult.Ready -> {
                        applyDelegateHint()
                        val ok = renderEngine.loadFaceLandmarkModel(r.path)
                        faceLandmarkState = if (ok) AiModelState.READY else AiModelState.ERROR
                        Log.i(TAG, "Face landmark model (TFLite) load=${ok}, path=${r.path}")
                        onResult?.invoke(ok)
                    }
                    is ModelAssetManager.ModelLoadResult.Stub -> {
                        faceLandmarkState = AiModelState.STUB
                        Log.w(TAG, "Face landmark model is a stub. Drop face_landmarker.task into assets/models/.")
                        onResult?.invoke(false)
                    }
                    is ModelAssetManager.ModelLoadResult.Error -> {
                        faceLandmarkState = AiModelState.ERROR
                        Log.e(TAG, "Face landmark load error: ${r.message}")
                        onResult?.invoke(false)
                    }
                }
            }
        }
    }

    // ── Hair Segmentation / Coloring ──────────────────────────────────────────

    /**
     * Extracts and loads the hair segmentation model.
     * @param onResult  called with `true` = model ready, `false` = stub/error
     */
    fun enableHairColoring(onResult: ((Boolean) -> Unit)? = null) {
        hairModelState = AiModelState.LOADING
        scope.launch {
            when (val r = assetManager.extractModel(ASSET_HAIR_MODEL)) {
                is ModelAssetManager.ModelLoadResult.Ready -> {
                    applyDelegateHint()
                    val ok = renderEngine.loadHairModel(r.path)
                    hairModelState = if (ok) AiModelState.READY else AiModelState.ERROR
                    Log.i(TAG, "Hair model load=${ok}, path=${r.path}")
                    onResult?.invoke(ok)
                }
                is ModelAssetManager.ModelLoadResult.Stub -> {
                    hairModelState = AiModelState.STUB
                    Log.w(TAG, "Hair model is a stub. Replace ${r.assetName} with a real model.")
                    onResult?.invoke(false)
                }
                is ModelAssetManager.ModelLoadResult.Error -> {
                    hairModelState = AiModelState.ERROR
                    Log.e(TAG, "Hair model load error: ${r.message}")
                    onResult?.invoke(false)
                }
            }
        }
    }

    // ── Beauty (no model required) ────────────────────────────────────────────

    /** Enables skin smoothing + whitening immediately (no TFLite model needed). */
    fun enableBeauty(smoothStrength: Float = 0.6f, whitenStrength: Float = 0.4f) {
        renderEngine.enableBeauty(smoothStrength, whitenStrength)
        Log.d(TAG, "Beauty enabled: smooth=$smoothStrength, whiten=$whitenStrength")
    }

    fun disableBeauty() {
        renderEngine.disableBeauty()
    }

    // ── Hand Landmark ─────────────────────────────────────────────────────────

    /**
     * 提取并加载手部 21 关键点模型（MediaPipe Hands .tflite）。
     * 加载成功后，[RenderEngine.getHandLandmarks] 将返回关键点数据。
     */
    fun enableHandTracking(onResult: ((Boolean) -> Unit)? = null) {
        handLandmarkState = AiModelState.LOADING
        scope.launch {
            when (val r = assetManager.extractModel(ASSET_HAND_MODEL)) {
                is ModelAssetManager.ModelLoadResult.Ready -> {
                    applyDelegateHint()
                    val ok = renderEngine.loadHandModel(r.path)
                    handLandmarkState = if (ok) AiModelState.READY else AiModelState.ERROR
                    Log.i(TAG, "Hand landmark model load=$ok, path=${r.path}")
                    onResult?.invoke(ok)
                }
                is ModelAssetManager.ModelLoadResult.Stub -> {
                    handLandmarkState = AiModelState.STUB
                    Log.w(TAG, "Hand model is a stub. Replace ${r.assetName} with a real model.")
                    onResult?.invoke(false)
                }
                is ModelAssetManager.ModelLoadResult.Error -> {
                    handLandmarkState = AiModelState.ERROR
                    Log.e(TAG, "Hand model load error: ${r.message}")
                    onResult?.invoke(false)
                }
            }
        }
    }

    // ── Portrait Segmentation ─────────────────────────────────────────────────

    /**
     * 提取并加载人像分割模型。
     * 加载成功后调用 [RenderEngine.setSegmentationMode] 选择背景处理方式。
     * @param segMode  0=模糊背景（默认）1=纯色背景 2=透明 3=图片背景
     */
    fun enableSegmentation(
        segMode: Int = 0,
        bgColorArgb: Int = 0xFF000000.toInt(),
        blurStrength: Float = 15f,
        onResult: ((Boolean) -> Unit)? = null
    ) {
        segmentationState = AiModelState.LOADING
        scope.launch {
            when (val r = assetManager.extractModel(ASSET_SEG_MODEL)) {
                is ModelAssetManager.ModelLoadResult.Ready -> {
                    applyDelegateHint()
                    val ok = renderEngine.loadSegmentationModel(r.path)
                    if (ok) renderEngine.setSegmentationMode(segMode, bgColorArgb, blurStrength)
                    segmentationState = if (ok) AiModelState.READY else AiModelState.ERROR
                    Log.i(TAG, "Segmentation model load=$ok, mode=$segMode")
                    onResult?.invoke(ok)
                }
                is ModelAssetManager.ModelLoadResult.Stub -> {
                    segmentationState = AiModelState.STUB
                    Log.w(TAG, "Segmentation model is a stub. Replace ${r.assetName} with a real model.")
                    onResult?.invoke(false)
                }
                is ModelAssetManager.ModelLoadResult.Error -> {
                    segmentationState = AiModelState.ERROR
                    Log.e(TAG, "Segmentation model load error: ${r.message}")
                    onResult?.invoke(false)
                }
            }
        }
    }

    private fun applyDelegateHint() {
        val delegate = delegateStrategy.select(delegatePreference)
        delegateStrategy.applyToNative(renderEngine.getNativeHandle(), delegate)
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────

    /** Clears extracted model files from private storage (e.g., on logout/uninstall). */
    fun clearModelCache() {
        faceLandmarkerHelper?.close()
        faceLandmarkerHelper = null
        assetManager.clearCache()
        faceLandmarkState  = AiModelState.IDLE
        hairModelState     = AiModelState.IDLE
        handLandmarkState  = AiModelState.IDLE
        segmentationState  = AiModelState.IDLE
    }
}
