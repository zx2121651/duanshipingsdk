package com.sdk.video

import android.graphics.SurfaceTexture
import android.view.Surface
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.*

// 定义支持的视频滤镜类型枚举
enum class VideoFilterType {
    BRIGHTNESS, GAUSSIAN_BLUR, LOOKUP, BILATERAL, CINEMATIC_LOOKUP, COMPUTE_BLUR,
    NIGHT_VISION, LUT3D, DUAL_KAWASE_BLUR, BLOOM,
    PROP_OVERLAY  // Real-time prop/sticker overlay — maps to FilterType::PROP_OVERLAY = 10
}

// 引擎运行状态，供上层 UI 监听，以便在发生错误或退化渲染时给出提示
enum class FilterEngineState {
    STOPPED, INITIALIZING, RUNNING, DEGRADED, ERROR
}

/**
 * 跨平台实时视频滤镜的 Android 端门面类 (Facade)。
 * 核心设计：
 * 1. 使用 Kotlin 协程和专属的单线程调度器 (GLRenderThread) 来保障 OpenGL 上下文的线程安全。
 * 2. 使用 SharedFlow (共享数据流) 作为生产者-消费者模型，向外界抛出处理后的纹理 ID，解耦底层渲染与上层 UI。
 */
@OptIn(InternalApi::class, ExperimentalCoroutinesApi::class)
class VideoFilterManager(private val context: android.content.Context,
    private val width: Int,
    private val height: Int,
    // 允许外部传入协程作用域，默认创建一个后台任务域
    val scope: CoroutineScope = CoroutineScope(Dispatchers.Default + SupervisorJob())
) {
    private var videoEncoder: VideoEncoder? = null

    // 实例化底层的 C++ JNI 包装类
    val renderEngine = RenderEngine(width, height)

    // 输入的 Surface，将提供给相机硬件 (如 CameraX) 捕获画面流
    private var inputSurface: Surface? = null

    // 重点：为所有的 GL 操作分配一个专属的单线程。
    // 在 OpenGL 中，所有的 Context 和 Texture ID 都绑定在特定的线程上。
    // 必须确保所有对 renderEngine 的调用都在这个单线程中进行。

    /**
     * GL Thread Dispatcher: External consumers (like GLSurfaceView or a custom HandlerThread)
     * must provide this callback to route tasks into the correct EGL context thread.
     */
    var glThreadDispatcher: ((Runnable) -> Unit)? = null

    @Volatile
    private var glThreadId: Long = -1

    /**
     * Internal helper to dispatch GL tasks and suspend the caller until completion.
     * This enforces the single-threaded rendering constraint of the Core C++ engine.
     *
     * @param action The GL task to execute.
     * @return Result of the task.
     */
    @OptIn(ExperimentalCoroutinesApi::class)
    private suspend fun <T> runOnGLThread(action: () -> Result<T>): Result<T> {
        // [Optimization]: If already on GL thread, execute immediately to reduce latency
        if (glThreadId != -1L && Thread.currentThread().id == glThreadId) {
            return try { action() } catch (e: Exception) { Result.failure(e) }
        }

        val dispatcher = glThreadDispatcher
            ?: return Result.failure(IllegalStateException("GL thread dispatcher not bound. Cannot execute GL action."))

        return suspendCancellableCoroutine { continuation ->
            val runnable = Runnable {
                try {
                    val result = action()
                    if (continuation.isActive) {
                        continuation.resume(result) { /* cancellation cleanup */ }
                    }
                } catch (e: Exception) {
                    if (continuation.isActive) {
                        continuation.resume(Result.failure(e)) { }
                    }
                }
            }
            dispatcher.invoke(runnable)
        }
    }

    // StateFlow 暴露引擎状态，外部 UI (如 Compose) 可以直接 collectAsState() 监听变化
    private val _engineState = MutableStateFlow(FilterEngineState.STOPPED)
    val engineState: StateFlow<FilterEngineState> = _engineState.asStateFlow()

    /**
     * 直接回调（绕过协程/SharedFlow）：在 GL 线程直接通知显示层。
     * FilterCameraPreview 设置此回调以实现零延迟帧更新。
     */
    var onFrameOutput: ((Int) -> Unit)? = null

    /**
     * 核心输出流：向外发射每次渲染完成后的纹理 ID。
     * 为什么使用 BufferOverflow.DROP_OLDEST？
     * 当底层 GPU 渲染帧率过高（比如 60fps），而上层 UI 消费能力不足时，
     * 直接丢弃旧帧（即降帧），避免堆积导致内存溢出 (OOM)。这就是典型的防背压 (Backpressure) 降级策略。
     */
    private val _processedFrames = MutableSharedFlow<Result<Int>>(
        extraBufferCapacity = 1,
        onBufferOverflow = BufferOverflow.DROP_OLDEST
    )
    val processedFrames: SharedFlow<Result<Int>> = _processedFrames.asSharedFlow()

    private val _performanceMetrics = MutableStateFlow<RenderEngine.PerformanceMetrics?>(null)
    val performanceMetrics: StateFlow<RenderEngine.PerformanceMetrics?> = _performanceMetrics.asStateFlow()

    /**
     * Unified error handling for both synchronous initialization and asynchronous rendering errors.
     * Categorizes errors into fatal (ERROR) or recoverable (DEGRADED).
     */
    private fun handleError(errorCode: Int, errorMessage: String) {
        val isFatal = VideoSdkError.isFatal(errorCode)
        val newState = if (isFatal) FilterEngineState.ERROR else FilterEngineState.DEGRADED

        // Update engine state (but don't overwrite ERROR with DEGRADED)
        if (_engineState.value != FilterEngineState.ERROR) {
            _engineState.value = newState
        }

        // Propagate failure to the processed frames flow
        val error = VideoSdkError.NativeError(errorCode, errorMessage)
        _processedFrames.tryEmit(Result.failure(error))

        if (isFatal) {
            Log.e("VideoFilterManager", "FATAL ENGINE ERROR: $errorMessage")
        } else {
            Log.w("VideoFilterManager", "Engine degraded: $errorMessage")
        }
    }

    // 初始化引擎，必须切换到专属的 GL 线程
    suspend fun initialize(): Result<Unit> {
        return runOnGLThread {
            _engineState.value = FilterEngineState.INITIALIZING

            // Capture GL thread ID for optimization
            glThreadId = Thread.currentThread().id

            val res = renderEngine.init(context.assets)
            if (res != 0) {
                handleError(res, "Initialization failed")
                return@runOnGLThread Result.failure(VideoSdkError.fromNativeCode(res))
            }

            // 监听底层渲染错误 (Callback from JNI)
            renderEngine.onRenderErrorListener = { errorCode, errorMessage ->
                handleError(errorCode, errorMessage)
            }

            // 监听性能数据
            renderEngine.onPerformanceUpdateListener = { _ ->
                // 每次性能回调时主动拉取一次 metrics 并推到流里
                _performanceMetrics.value = renderEngine.getMetrics()
            }

            // 设置底层每处理完一帧的回调监听
            renderEngine.onFrameProcessedListener = { outputTexId ->
                // Note: outputTexId < 0 cases are now handled by onRenderErrorListener
                if (outputTexId >= 0) {
                    // 双通道通知：
                    // 1. 直接回调（GL 线程，零延迟）→ 显示层立即同步纹理 ID
                    onFrameOutput?.invoke(outputTexId)
                    // 2. SharedFlow（协程消费）→ 供录制等其他消费者使用
                    val emitted = _processedFrames.tryEmit(Result.success(outputTexId))
                    if (!emitted) {
                        renderEngine.recordDroppedFrame()
                    }
                }
            }

            // 获取底层的 OES 纹理载体，包装为 Android 的 Surface，供 CameraX 使用
            val st = renderEngine.getSurfaceTexture()
            if (st != null) {
                // -------------------------------------------------------
                // 关键修复：RenderEngine.init() 内部调用了
                //   surfaceTexture?.setOnFrameAvailableListener(this)
                // 但没有传入 Handler，Android 会把回调派发到主线程 (Main Looper)。
                // 主线程没有绑定 EGL Context，在主线程调用 updateTexImage() 会导致
                //   "checkAndUpdateEglState: invalid current EGLDisplay"
                //   → IllegalStateException: Unable to update texture contents
                //
                // 解决方案：覆盖监听器，将帧回调通过 glThreadDispatcher 路由到
                // GL 线程执行，确保 updateTexImage() 始终在有 EGL Context 的线程
                // 上被调用。
                // -------------------------------------------------------
                st.setOnFrameAvailableListener { surfaceTex ->
                    val dispatcher = glThreadDispatcher
                    if (dispatcher != null) {
                        dispatcher.invoke {
                            renderEngine.onFrameAvailable(surfaceTex)
                        }
                    } else {
                        // Dispatcher not yet bound — skip frame to avoid EGL crash.
                        // This may happen during very early startup before the GL
                        // surface is attached; frames will resume once dispatcher
                        // is set.
                        Log.w("VideoFilterManager",
                            "onFrameAvailable: glThreadDispatcher not bound, skipping frame")
                    }
                }

                inputSurface = Surface(st)
                _engineState.value = FilterEngineState.RUNNING
                Result.success(Unit)
            } else {
                val e = Exception("Failed to create input surface")
                handleError(-1001, e.message ?: "Unknown surface failure")
                Result.failure(e)
            }
        }.onFailure { e ->
            val code = if (e is VideoSdkError) e.code else -1
            handleError(code, e.message ?: "Unknown async failure")
        }
    }

    suspend fun awaitInputSurface(): Surface {
        // 彻底告别脆弱的 delay(500)，使用协程 Flow 挂起等待引擎真正初始化完毕
        val state = engineState.first { it == FilterEngineState.RUNNING || it == FilterEngineState.ERROR }
        if (state == FilterEngineState.ERROR) {
            throw IllegalStateException("Engine failed to initialize (state is ERROR)")
        }
        return inputSurface ?: throw IllegalStateException("Surface is null even though engine is RUNNING")
    }

    // 提供给相机的输入层
    fun getInputSurface(): Surface? {
        return inputSurface
    }

    // 动态添加滤镜
    suspend fun addFilter(type: VideoFilterType): Result<Unit> {
        return runOnGLThread {
            val typeInt = when (type) {
                VideoFilterType.BRIGHTNESS        -> RenderEngine.FILTER_TYPE_BRIGHTNESS
                VideoFilterType.GAUSSIAN_BLUR     -> RenderEngine.FILTER_TYPE_GAUSSIAN_BLUR
                VideoFilterType.LOOKUP            -> RenderEngine.FILTER_TYPE_LOOKUP
                VideoFilterType.BILATERAL         -> RenderEngine.FILTER_TYPE_BILATERAL
                VideoFilterType.CINEMATIC_LOOKUP  -> 4
                VideoFilterType.COMPUTE_BLUR      -> 5
                VideoFilterType.NIGHT_VISION      -> RenderEngine.FILTER_TYPE_NIGHT_VISION
                VideoFilterType.LUT3D             -> RenderEngine.FILTER_TYPE_LUT3D
                VideoFilterType.DUAL_KAWASE_BLUR  -> RenderEngine.FILTER_TYPE_DUAL_KAWASE_BLUR
                VideoFilterType.BLOOM             -> RenderEngine.FILTER_TYPE_BLOOM
                VideoFilterType.PROP_OVERLAY      -> RenderEngine.FILTER_TYPE_PROP_OVERLAY
            }
            renderEngine.addFilter(typeInt)
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // 抖音对标 facade：Beauty / FaceReshape / Makeup / HairSeg / EffectPlugin
    // 全部通过 GL 线程调度，保证与底层 RenderEngine 单线程契约一致。
    // ──────────────────────────────────────────────────────────────────────

    /** 开启美颜（磨皮 + 美白），无需模型即可使用。 */
    suspend fun enableBeauty(smoothStrength: Float = 0.6f,
                              whitenStrength: Float = 0.4f): Result<Unit> {
        return runOnGLThread {
            renderEngine.enableBeauty(smoothStrength, whitenStrength)
            Result.success(Unit)
        }
    }

    /** 关闭美颜，并重建管线（剔除 BeautyFilter）。 */
    suspend fun disableBeauty(): Result<Unit> {
        return runOnGLThread {
            renderEngine.disableBeauty()
            Result.success(Unit)
        }
    }

    /**
     * 加载人脸关键点 TFLite 模型。stub 模式下传任意路径都会返回 true。
     */
    suspend fun loadFaceLandmarkModel(modelPath: String): Result<Boolean> {
        return runOnGLThread {
            Result.success(renderEngine.loadFaceLandmarkModel(modelPath))
        }
    }

    /** 设置人脸重塑参数（大眼/瘦脸/瘦鼻/抬额/V下巴/嘴宽）。 */
    suspend fun setFaceReshape(eyeScale: Float = 0f,
                                faceSlim: Float = 0f,
                                noseSlim: Float = 0f,
                                foreheadUp: Float = 0f,
                                chinV: Float = 0f,
                                mouthWidth: Float = 0f): Result<Unit> {
        return runOnGLThread {
            renderEngine.setFaceReshape(eyeScale, faceSlim, noseSlim,
                                         foreheadUp, chinV, mouthWidth)
            Result.success(Unit)
        }
    }

    /** 设置某一层化妆参数（口红/腮红/眼影/高光/修容/眉）。 */
    suspend fun setMakeup(layer: RenderEngine.MakeupLayer,
                          r: Float, g: Float, b: Float,
                          intensity: Float): Result<Unit> {
        return runOnGLThread {
            renderEngine.setMakeup(layer, r, g, b, intensity)
            Result.success(Unit)
        }
    }

    /** 加载头发分割 TFLite 模型。 */
    suspend fun loadHairModel(modelPath: String): Result<Boolean> {
        return runOnGLThread {
            Result.success(renderEngine.loadHairModel(modelPath))
        }
    }

    /** 设置头发染色目标颜色 + 染色/高光强度。 */
    suspend fun setHairColor(r: Float, g: Float, b: Float,
                              colorIntensity: Float = 0.7f,
                              glossIntensity: Float = 0.3f): Result<Unit> {
        return runOnGLThread {
            renderEngine.setHairColor(r, g, b, colorIntensity, glossIntensity)
            Result.success(Unit)
        }
    }

    /**
     * 加载特效包（解析 manifest.json + 注册到 EffectPluginManager）。
     * @return 特效 id；失败时为空字符串。
     */
    suspend fun loadEffect(effectRoot: String): Result<String> {
        return runOnGLThread {
            Result.success(renderEngine.loadEffect(effectRoot))
        }
    }

    /** 激活指定特效 id；传空字符串等同于 deactivate。 */
    suspend fun activateEffect(effectId: String): Result<Unit> {
        return runOnGLThread {
            renderEngine.activateEffect(effectId)
            Result.success(Unit)
        }
    }

    /** 停用并清空全部已加载特效。 */
    suspend fun deactivateAllEffects(): Result<Unit> {
        return runOnGLThread {
            renderEngine.deactivateAllEffects()
            Result.success(Unit)
        }
    }

    /**
     * 同步获取设备能力（不需要 GL 线程；底层只是读取已嗅探好的字段）。
     * 引擎未初始化时返回 null。
     */
    fun getDeviceCapabilities(): RenderEngine.DeviceCapabilities? =
        renderEngine.getDeviceCapabilities()

    // 清空滤镜管线
    suspend fun removeAllFilters(): Result<Unit> {
        return runOnGLThread { renderEngine.removeAllFilters() }
    }

    // 更新滤镜参数 (Float)
    suspend fun updateParameter(key: String, value: Float): Result<Unit> {
        return runOnGLThread { renderEngine.updateParameterFloat(key, value) }
    }

    // 更新滤镜参数 (Int)
    suspend fun updateParameter(key: String, value: Int): Result<Unit> {
        return runOnGLThread { renderEngine.updateParameterInt(key, value) }
    }


    // --- 音视频录制代理方法 ---

    suspend fun startVideoRecording(config: VideoExportConfig): Result<Unit> {
        val encoder = VideoEncoder(this, config)
        val startResult = encoder.startRecording()

        if (startResult.isFailure) {
            return Result.failure(startResult.exceptionOrNull() ?: Exception("Unknown encoder failure"))
        }

        val surface = startResult.getOrThrow()
        val glResult = runOnGLThread {
            videoEncoder = encoder
            renderEngine.setRecordingAnchor(encoder.getStartTimeNs())
            renderEngine.startRecording(surface)
        }

        if (glResult.isFailure) {
            encoder.stopRecording()
            videoEncoder = null
        }
        return glResult
    }

    suspend fun stopVideoRecording(isFallback: Boolean = false): Result<Unit> {
        val res = runOnGLThread { renderEngine.stopRecording() }
        videoEncoder?.stopRecording(isFallback)
        videoEncoder = null
        return res
    }

    fun setRecordingSpeed(speed: Float) {
        renderEngine.speedRate = speed
    }

    // Oboe 音频控制不需要强制在 GL 线程，但为了统一管理也可以放进来
    fun startAudioRecord(sampleRate: Int): Result<Unit> {
        return renderEngine.startAudioRecord(sampleRate)
    }

    fun stopAudioRecord(): Result<Unit> {
        return renderEngine.stopAudioRecord()
    }

    fun readAudioPCM(buffer: ByteArray, length: Int): Int {
        return renderEngine.readAudioPCM(buffer, length)
    }

    fun getAudioTimeNs(): Long {
        return renderEngine.getAudioTimeNs()
    }

    // 暴露性能监控监听器供外部（或者修改为 StateFlow 抛出）
    // 手动触发一帧处理
    fun processFrame() {
        renderEngine.getSurfaceTexture()?.let { st ->
            renderEngine.onFrameAvailable(st)
        }
    }

    fun markFrameRendered(textureId: Int) {
        renderEngine.markFrameRendered(textureId)
    }


    // 释放所有的硬件及线程资源

    suspend fun updateShaderSource(name: String, source: String): Result<Unit> {
        return runOnGLThread {
            val res = renderEngine.updateShaderSource(name, source)
            if (res == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(res))
        }
    }

    // ── 手部关键点 ────────────────────────────────────────────────────────

    suspend fun loadHandModel(modelPath: String): Result<Unit> = runOnGLThread {
        if (renderEngine.loadHandModel(modelPath)) Result.success(Unit)
        else Result.failure(VideoSdkError.ModelLoadFailed("hand_landmark"))
    }

    /** 获取最新帧手部关键点（可在任意线程调用）。返回 null 表示未检测到手。 */
    fun getHandLandmarks(): FloatArray? = renderEngine.getHandLandmarks()

    // ── 人像分割 ──────────────────────────────────────────────────────────

    suspend fun loadSegmentationModel(modelPath: String): Result<Unit> = runOnGLThread {
        if (renderEngine.loadSegmentationModel(modelPath)) Result.success(Unit)
        else Result.failure(VideoSdkError.ModelLoadFailed("segmentation"))
    }

    /** 0=模糊背景 1=纯色背景 2=透明 3=图片背景 */
    suspend fun setSegmentationMode(
        mode: Int,
        bgColorArgb: Int = 0xFF000000.toInt(),
        blurStrength: Float = 15f
    ): Result<Unit> = runOnGLThread {
        renderEngine.setSegmentationMode(mode, bgColorArgb, blurStrength)
        Result.success(Unit)
    }

    // ── 绿幕/色度键 ───────────────────────────────────────────────────────

    suspend fun enableChromaKey(
        hueCenter: Float = 0.333f,
        hueTol: Float = 0.10f,
        satMin: Float = 0.25f,
        edgeSoftness: Float = 0.06f
    ): Result<Unit> = runOnGLThread {
        renderEngine.enableChromaKey(hueCenter, hueTol, satMin, edgeSoftness)
        Result.success(Unit)
    }

    suspend fun disableChromaKey(): Result<Unit> = runOnGLThread {
        renderEngine.disableChromaKey()
        Result.success(Unit)
    }

    // ── 人脸形变 ──────────────────────────────────────────────────────────

    /** effectIndex: 0=瘦脸 1=大眼 2=下颌 3=额头 4=鼻翼 5=嘴型 6=眼距 7=下巴 */
    suspend fun setFaceMorphStrength(effectIndex: Int, strength: Float): Result<Unit> = runOnGLThread {
        renderEngine.setFaceMorphStrength(effectIndex, strength)
        Result.success(Unit)
    }

    suspend fun resetFaceMorph(): Result<Unit> = runOnGLThread {
        renderEngine.resetFaceMorph()
        Result.success(Unit)
    }

    // ── 身体特效 ──────────────────────────────────────────────────────────

    /** effectIndex: 0=瘦身 1=长腿 2=小头 3=窄肩 4=提臀 */
    suspend fun setBodyEffectStrength(effectIndex: Int, strength: Float): Result<Unit> = runOnGLThread {
        renderEngine.setBodyEffectStrength(effectIndex, strength)
        Result.success(Unit)
    }

    suspend fun resetBodyEffect(): Result<Unit> = runOnGLThread {
        renderEngine.resetBodyEffect()
        Result.success(Unit)
    }

    /** poseData: FloatArray[17*3] COCO格式，由 BodyPoseDetector 产生后调用。 */
    fun updateBodyPose(poseData: FloatArray) = renderEngine.updateBodyPose(poseData)

    // ── 表情检测 ──────────────────────────────────────────────────────────

    /** 无需模型，基于已加载的人脸关键点实时计算。返回 null 表示无人脸。 */
    fun getExpressions(): FloatArray? = renderEngine.getExpressions()

    fun release() {
        // 1. 先停掉录制和音频，防止异步任务还在调用 renderEngine
        videoEncoder?.stopRecording()
        videoEncoder = null
        stopAudioRecord()

        inputSurface?.release()
        inputSurface = null
        _engineState.value = FilterEngineState.STOPPED

        // 毒药屏障：将释放动作排入 GL 队列末尾！确保之前的渲染任务跑完
        val dispatcher = glThreadDispatcher
        if (dispatcher != null) {
            // 将释放命令丢至 GL 线程队列的绝对末尾
            dispatcher.invoke {
                try {
                    renderEngine.stopRecording()
                    renderEngine.release()
                    Log.i("VideoFilterManager", "RenderEngine safely deleted via Poison Pill.")
                } catch (e: Exception) {
                    Log.e("VideoFilterManager", "Error in delayed release", e)
                }
            }
        } else {
            // 调度器未绑定的情况下，安全进行同步释放
            try { renderEngine.release() } catch (e: Exception) {}
        }

        // 安全中止外层监听器
        scope.cancel() // 取消协程域中的所有任务
    }
}
