package com.sdk.video

import android.graphics.SurfaceTexture
import android.opengl.GLES20
import android.view.Surface
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

@InternalApi
class RenderEngine(private val width: Int, private val height: Int) : SurfaceTexture.OnFrameAvailableListener {

    companion object {
        init {
            System.loadLibrary("video-sdk")
        }

        const val FILTER_TYPE_BRIGHTNESS = 0
        const val FILTER_TYPE_GAUSSIAN_BLUR = 1
        const val FILTER_TYPE_LOOKUP = 2
        const val FILTER_TYPE_BILATERAL = 3 // Skin Smoothing
        const val FILTER_TYPE_NIGHT_VISION = 6
        const val FILTER_TYPE_LUT3D = 7        // P1-2: 64x64x64 3D LUT
        const val FILTER_TYPE_DUAL_KAWASE_BLUR = 8 // Large-radius iterative Kawase blur
        const val FILTER_TYPE_BLOOM = 9            // Glow: threshold → blur → additive composite
        const val FILTER_TYPE_PROP_OVERLAY = 10
    }

    private var nativeHandle: Long = 0
    private val handleLock = ReentrantReadWriteLock()
    private var surfaceTexture: SurfaceTexture? = null
    private var oesTextureId: Int = -1
    private val transformMatrix = FloatArray(16)

    // Performance field updated via JNI reflection
    @JvmField
    var lastFrameTimeMs: Long = 0L

    private var recordingStartTimeNs: Long = 0L
    private var lastVideoPtsNs: Long = -1L
    private var firstFrameSessionTimestamp: Long = -1L
    private var isRecording: Boolean = false
    @Volatile
    var speedRate: Float = 1.0f

    @Volatile
    private var released: Boolean = false

    var onFrameProcessedListener: ((outputTextureId: Int, timestampNs: Long) -> Unit)? = null
    var onPerformanceUpdateListener: ((durationMs: Long) -> Unit)? = null
    var onRenderErrorListener: ((errorCode: Int, errorMessage: String) -> Unit)? = null

    // 确保被 R8 混淆器保留，供 C++ 回调
    @androidx.annotation.Keep
    private fun onNativeRenderError(errorCode: Int, errorMessage: String) {
        android.util.Log.e("RenderEngine", "FATAL NATIVE ERROR [$errorCode]: $errorMessage")
        // 将故障信号向外转发给负责业务逻辑的 Facade
        onRenderErrorListener?.invoke(errorCode, errorMessage)
    }

    // Call on GL thread to initialize

    fun updateShaderSource(name: String, source: String): Int {
        handleLock.read {
            if (nativeHandle != 0L) {
                try {
                    nativeUpdateShaderSource(nativeHandle, name, source)
                    return 0
                } catch (e: NativeRenderException) {
                    return e.errorCode
                }
            }
        }
        return -1001 // ERR_INIT_CONTEXT_FAILED
    }

    class PerformanceMetrics(
        val averageFrameTimeMs: Float,
        val p50FrameTimeMs: Float,
        val p90FrameTimeMs: Float,
        val p99FrameTimeMs: Float,
        val droppedFrames: Int
    )


    fun getMetrics(): PerformanceMetrics? {
        handleLock.read {
            if (nativeHandle == 0L) return null
            val arr = nativeGetMetrics(nativeHandle) ?: return null
            return PerformanceMetrics(arr[0], arr[1], arr[2], arr[3], arr[4].toInt())
        }
    }

    fun recordDroppedFrame() {
        handleLock.read {
            if (nativeHandle != 0L) {
                nativeRecordDroppedFrame(nativeHandle)
            }
        }
    }

    fun markFrameRendered(textureId: Int) {
        handleLock.read {
            if (nativeHandle != 0L) {
                nativeMarkFrameRendered(nativeHandle, textureId)
            }
        }
    }

    fun init(assetManager: android.content.res.AssetManager): Int {
        handleLock.write {
            if (nativeHandle != 0L) return 0
            try {
                nativeHandle = nativeInit(assetManager)
            } catch (e: NativeRenderException) {
                return e.errorCode
            }
            if (nativeHandle == 0L) return -1001 // ERR_INIT_CONTEXT_FAILED
        }

        val textures = IntArray(1)
        GLES20.glGenTextures(1, textures, 0)
        oesTextureId = textures[0]

        GLES20.glBindTexture(0x8D65, oesTextureId) // GL_TEXTURE_EXTERNAL_OES
        // [FIX] Use GL_LINEAR for MIN_FILTER.  GL_NEAREST on OES textures is non-standard
        // on some Mali/Adreno drivers and can trigger GL_INVALID_OPERATION when the
        // SurfaceTexture framework internally rebinds the texture with different sampler
        // state expectations.  The OES2RGBFilter shader samples with linear interpolation
        // anyway, so GL_LINEAR is the correct and universally safe choice.
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glBindTexture(0x8D65, 0)

        surfaceTexture = SurfaceTexture(oesTextureId)
        surfaceTexture?.setOnFrameAvailableListener(this)
        return 0
    }

    fun getSurfaceTexture(): SurfaceTexture? {
        return surfaceTexture
    }

    override fun onFrameAvailable(st: SurfaceTexture?) {
        if (st == null) return
        if (released) return

        handleLock.read {
            if (nativeHandle == 0L) return

            // [FIX] Drain any uncleared GL errors from the initialization chain
            // (shader compile, FBO creation, VAO/VBO setup, pipeline compile) BEFORE
            // Drain stale GL errors before updateTexImage().
            // GL_STATE_ERRORs may accumulate from the display layer's GLSurfaceView
            // renderer or older SDK pipeline stages.  If not drained here,
            // updateTexImage() will fail because pending errors poison the
            // GL error state machine.
            var err = GLES20.glGetError()
            while (err != GLES20.GL_NO_ERROR) {
                android.util.Log.w("RenderEngine",
                    "onFrameAvailable: stale GL error before updateTexImage: 0x${Integer.toHexString(err)}")
                err = GLES20.glGetError()
            }

            try {
                st.updateTexImage()
            } catch (e: Exception) {
                android.util.Log.w("RenderEngine",
                    "updateTexImage failed (EGL context likely lost): ${e.message}")
                return
            }
            st.getTransformMatrix(transformMatrix)

            // Pass timestamp to native layer for MediaCodec EGL presentation time
            // [PTS Anchor Fix]: Use the first frame's SurfaceTexture timestamp as a relative baseline
            // and offset it by the recording anchor. This maintains the camera's native cadence
            // while ensuring the track starts exactly at the synchronized anchor.
            var timestampNs = st.timestamp
            if (isRecording && recordingStartTimeNs > 0) {
                if (firstFrameSessionTimestamp == -1L) {
                    firstFrameSessionTimestamp = timestampNs
                }
                val elapsed = timestampNs - firstFrameSessionTimestamp
                timestampNs = recordingStartTimeNs + (elapsed / speedRate).toLong()

                if (lastVideoPtsNs != -1L && timestampNs <= lastVideoPtsNs) {
                    timestampNs = lastVideoPtsNs + 10000 // Ensure strict monotonicity (10us)
                }
                lastVideoPtsNs = timestampNs
            }

            val outputTexId = nativeProcessFrame(nativeHandle, oesTextureId, width, height, transformMatrix, timestampNs)

            // 如果返回值是负数，表示产生了错误
            if (outputTexId >= 0) {
                onFrameProcessedListener?.invoke(outputTexId, timestampNs)
                if (lastFrameTimeMs > 0) {
                    onPerformanceUpdateListener?.invoke(lastFrameTimeMs)
                }
            } else {
                // Defensive fallback: if native code returned an error but didn't call onNativeRenderError callback
                // (e.g., due to JNI reference issues), we manually notify the listener.
                onRenderErrorListener?.invoke(outputTexId, "Native frame processing failed with code: $outputTexId")
            }

            // Final drain — must run AFTER all listener callbacks because
            // onFrameOutput (set by FilterCameraPreview) performs GL rendering
            // to the display surface which can produce GL_INVALID_OPERATION on
            // Mali/Adreno.  The C++ post-exec drain handles errors from execute(),
            // but listener GL work happens here on the Kotlin side and would
            // otherwise leak 0x502 into the next frame's pre-updateTexImage check.
            var frameError = GLES20.glGetError()
            while (frameError != GLES20.GL_NO_ERROR) {
                android.util.Log.w("RenderEngine",
                    "processFrame: GL error after listener callbacks: 0x${Integer.toHexString(frameError)}")
                frameError = GLES20.glGetError()
            }
            Unit
        }
    }

    // Recording API
    fun renderToRecordingSurface(textureId: Int, timestampNs: Long) {
        handleLock.read {
            if (nativeHandle != 0L) {
                nativeRenderToRecordingSurface(nativeHandle, textureId, width, height, timestampNs)
            }
        }
    }

    fun setRecordingAnchor(anchorTimeNs: Long) {
        recordingStartTimeNs = anchorTimeNs
        lastVideoPtsNs = -1L
        firstFrameSessionTimestamp = -1L
    }

    fun startRecording(surface: Surface): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeSetRecordingSurface(nativeHandle, surface)
                isRecording = true
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    fun stopRecording(): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeSetRecordingSurface(nativeHandle, null)
                isRecording = false
                recordingStartTimeNs = 0L
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    // Parameters
    fun setFlip(horizontal: Boolean, vertical: Boolean): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeUpdateParameterBool(nativeHandle, "flipHorizontal", horizontal)
                nativeUpdateParameterBool(nativeHandle, "flipVertical", vertical)
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    fun updateParameterFloat(key: String, value: Float): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeUpdateParameterFloat(nativeHandle, key, value)
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    fun updateParameterInt(key: String, value: Int): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeUpdateParameterInt(nativeHandle, key, value)
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    // Pipeline
    fun addFilter(type: Int): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeAddFilter(nativeHandle, type)
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    fun removeAllFilters(): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeRemoveAllFilters(nativeHandle)
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    fun getNativeHandle(): Long = nativeHandle

    // Call on GL thread to release
    fun release() {
        released = true
        handleLock.write {
            if (nativeHandle != 0L) {
                nativeRelease(nativeHandle)
                nativeHandle = 0
            }
        }
        surfaceTexture?.release()
        surfaceTexture = null
        if (oesTextureId != -1) {
            val textures = intArrayOf(oesTextureId)
            GLES20.glDeleteTextures(1, textures, 0)
            oesTextureId = -1
        }
    }


    fun startAudioRecord(sampleRate: Int): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeStartAudioRecord(nativeHandle, sampleRate)
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    fun stopAudioRecord(): Result<Unit> {
        handleLock.read {
            if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
            return try {
                nativeStopAudioRecord(nativeHandle)
                Result.success(Unit)
            } catch (e: NativeRenderException) {
                Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
            }
        }
    }

    fun readAudioPCM(buffer: ByteArray, length: Int): Int {
        handleLock.read {
            return if (nativeHandle != 0L) nativeReadAudioPCM(nativeHandle, buffer, length) else 0
        }
    }

    fun getAudioTimeNs(): Long {
        handleLock.read {
            return if (nativeHandle != 0L) nativeGetAudioTimeNs(nativeHandle) else 0L
        }
    }

    /**
     * P1-6: Call from the GL thread immediately after the EGL context is reported lost
     * (e.g. EGL_CONTEXT_LOST from eglMakeCurrent). Clears all CPU-side GL state.
     */
    fun onContextLost() {
        handleLock.read {
            if (nativeHandle != 0L) nativeOnContextLost(nativeHandle)
        }
    }

    /**
     * P1-6: Call from the GL thread once a new EGL context has been made current.
     * Re-initializes shaders and FBOs on the new context.
     * @return 0 on success, negative error code on failure.
     */
    fun onContextRestored(): Int {
        handleLock.read {
            return if (nativeHandle != 0L) nativeOnContextRestored(nativeHandle) else
                -1
        }
    }

    /**
     * P2-13: 启用动态分辨率缩放（DSR）。
     *
     * 当平均帧时间超过目标 10% 时自动降低渲染分辨率，帧时间恢复后缓慢回升。
     * 配置在下一次 [TimelineExporter.exportAsync] 调用时生效。
     *
     * @param targetFps  目标帧率（Hz），超过则降低分辨率
     * @param minScale   允许的最低分辨率倍率（默认 0.5 = 50%）
     * @param maxScale   允许的最高分辨率倍率（默认 1.0 = 原始分辨率）
     */
    fun setDsrConfig(targetFps: Float, minScale: Float = 0.5f, maxScale: Float = 1.0f) {
        handleLock.read {
            if (nativeHandle != 0L) nativeSetDsrConfig(nativeHandle, targetFps, minScale, maxScale)
        }
    }

    /**
     * P2-14: Android ComponentCallbacks2.onTrimMemory 钩子。
     *
     * 根据 [level] 分 5 档收缩 GPU FBO 缓存占用：
     * - >= 80 (TRIM_MEMORY_COMPLETE)        → 清空所有 FBO，上限  32 MB
     * - >= 40 (TRIM_MEMORY_BACKGROUND)      → 清空所有 FBO，上限  64 MB
     * - >= 20 (TRIM_MEMORY_UI_HIDDEN)       → 同上
     * - >= 15 (TRIM_MEMORY_RUNNING_CRITICAL)→ 上限  64 MB（LRU 驱逐）
     * - >= 10 (TRIM_MEMORY_RUNNING_LOW)     → 上限 128 MB
     * - >=  5 (TRIM_MEMORY_RUNNING_MODERATE)→ 上限 192 MB
     *
     * 推荐用 [VideoSdkMemoryCallbacks] 自动绑定：
     * ```
     * application.registerComponentCallbacks(VideoSdkMemoryCallbacks(renderEngine))
     * ```
     */
    fun onTrimMemory(level: Int) {
        handleLock.read {
            if (nativeHandle != 0L) nativeOnTrimMemory(nativeHandle, level)
        }
    }

    // ----------------------------------------------------------------
    // RHI 后端选择 API
    // ----------------------------------------------------------------

    /** RHI 后端类型（与 C++ BackendType 枚举对齐，值相同） */
    enum class BackendType(val value: Int) {
        AUTO(0),    /** 自动选择（优先级：Metal > Vulkan > GLES） */
        GLES(1),    /** OpenGL ES（3.0 / 3.1 / 3.2 三级梯级） */
        VULKAN(2),  /** Vulkan NDK（Android） */
        METAL(3);   /** Metal（iOS/macOS，Android 不可用） */

        companion object {
            fun fromInt(v: Int) = entries.firstOrNull { it.value == v } ?: GLES
        }
    }

    /**
     * 设置首选渲染后端（必须在 initialize() 之前调用）。
     * 实际使用的后端通过 [getActiveBackend] 查询。
     */
    fun setPreferredBackend(backend: BackendType) {
        handleLock.read {
            if (nativeHandle != 0L) nativeSetBackend(nativeHandle, backend.value)
        }
    }

    /** 返回当前实际使用的渲染后端。 */
    fun getActiveBackend(): BackendType {
        handleLock.read {
            return BackendType.fromInt(
                if (nativeHandle != 0L) nativeGetActiveBackend(nativeHandle) else 1
            )
        }
    }

    /** 返回设备实际支持的 GLES 版本梯级（30 / 31 / 32）。 */
    fun getGLESVersion(): Int {
        handleLock.read {
            return if (nativeHandle != 0L) nativeGetGLESVersion(nativeHandle) else 30
        }
    }

    // ----------------------------------------------------------------
    // 磨皮美白 — 无需模型，立即可用
    // ----------------------------------------------------------------

    /**
     * 预热美颜滤镜：编译 shader、分配 PBO/VBO，避免点击美颜开关时首次初始化造成管线卡顿。
     * 应在引擎初始化完成后立即调用。
     * @return true 成功
     */
    fun preWarmBeauty(): Boolean {
        handleLock.read {
            if (nativeHandle == 0L) return false
            val err = nativePreWarmBeauty(nativeHandle)
            if (err != 0) {
                android.util.Log.w("RenderEngine", "Beauty pre-warm failed: $err")
                return false
            }
            return true
        }
        return false
    }

    /**
     * 启用磨皮美白效果（基于 YCbCr 皮肤检测 + 近似双边模糊，无需 TFLite）。
     * 首次调用将预热的 BeautyFilter 加入管线（触发一次 rebuild），后续仅更新强度。
     * @return 0 成功，非零失败
     */
    fun enableBeauty(smoothStrength: Float = 0.6f, whitenStrength: Float = 0.4f): Int {
        handleLock.read {
            if (nativeHandle != 0L)
                return nativeEnableBeauty(nativeHandle, smoothStrength, whitenStrength)
        }
        return -1
    }

    /** 关闭磨皮美白（管线中剔除 BeautyFilter）。@return 0 成功 */
    fun disableBeauty(): Int {
        handleLock.read {
            if (nativeHandle != 0L) return nativeDisableBeauty(nativeHandle)
        }
        return -1
    }

    // ----------------------------------------------------------------
    // 人脸 AI — 对标抖音
    // ----------------------------------------------------------------

    /** 美妆层类型，与 C++ MakeupFilter 对齐 */
    enum class MakeupLayer(val value: Int) {
        LIP(0), BLUSH(1), EYESHADOW(2), HIGHLIGHT(3), CONTOUR(4), EYEBROW(5)
    }

    /**
     * 加载人脸关键点模型（.tflite）。
     * @param modelPath  assets 解压后的绝对路径
     */
    fun loadFaceLandmarkModel(modelPath: String): Boolean {
        handleLock.read {
            return if (nativeHandle != 0L) nativeLoadFaceLandmarkModel(nativeHandle, modelPath)
            else false
        }
    }

    /**
     * 获取最新帧人脸关键点，返回 FloatArray[212]（106点 × [x,y]），无人脸时返回 null。
     */
    fun getFaceLandmarks(): FloatArray? {
        handleLock.read {
            return if (nativeHandle != 0L) nativeGetFaceLandmarks(nativeHandle) else null
        }
    }

    /**
     * 设置人脸重塑参数（大眼/瘦脸/V脸/瘦鼻/额头/嘴型），参数范围 [0, 1]，0 = 关闭。
     */
    fun setFaceReshape(
        eyeScale: Float   = 0f,
        faceSlim: Float   = 0f,
        noseSlim: Float   = 0f,
        foreheadUp: Float = 0f,
        chinV: Float      = 0f,
        mouthWidth: Float = 0f
    ) {
        handleLock.read {
            if (nativeHandle != 0L)
                nativeSetFaceReshape(nativeHandle, eyeScale, faceSlim, noseSlim,
                                     foreheadUp, chinV, mouthWidth)
        }
    }

    /**
     * 设置美妆层颜色和强度。
     * @param layer      [MakeupLayer] 枚举
     * @param r,g,b      RGB 颜色 [0,1]
     * @param intensity  强度 [0,1]
     */
    fun setMakeup(layer: MakeupLayer, r: Float, g: Float, b: Float, intensity: Float) {
        handleLock.read {
            if (nativeHandle != 0L)
                nativeSetMakeup(nativeHandle, layer.value, r, g, b, intensity)
        }
    }

    /**
     * 加载发色分割模型（.tflite）。
     */
    fun loadHairModel(modelPath: String): Boolean {
        handleLock.read {
            return if (nativeHandle != 0L) nativeLoadHairModel(nativeHandle, modelPath)
            else false
        }
    }

    /**
     * 设置发色。
     * @param r,g,b          目标颜色 [0,1]
     * @param colorIntensity 染色强度 [0,1]
     * @param glossIntensity 高光强度 [0,1]
     */
    fun setHairColor(r: Float, g: Float, b: Float,
                     colorIntensity: Float = 0.7f, glossIntensity: Float = 0.3f) {
        handleLock.read {
            if (nativeHandle != 0L)
                nativeSetHairColor(nativeHandle, r, g, b, colorIntensity, glossIntensity)
        }
    }

    /**
     * 加载特效包（目录内含 manifest.json）。
     * @param effectRoot  特效包根目录绝对路径
     * @return  特效 ID（失败返回空字符串）
     */
    fun loadEffect(effectRoot: String): String {
        handleLock.read {
            return if (nativeHandle != 0L) nativeLoadEffect(nativeHandle, effectRoot) else ""
        }
    }

    /** 激活特效（传空字符串 = 关闭所有） */
    fun activateEffect(effectId: String) {
        handleLock.read {
            if (nativeHandle != 0L) nativeActivateEffect(nativeHandle, effectId)
        }
    }

    /** 关闭所有特效 */
    fun deactivateAllEffects() {
        handleLock.read {
            if (nativeHandle != 0L) nativeDeactivateAllEffects(nativeHandle)
        }
    }

    /**
     * 设备 GL 能力快照（用于诊断页 UI）。
     * 由 GLContextManager 嗅探后聚合返回，所有字段均反映真实硬件能力。
     */
    data class DeviceCapabilities(
        val glesVersion: Int,         // 30 / 31 / 32
        val computeShader: Boolean,   // 是否支持 Compute Shader
        val fp16: Boolean,            // 是否支持 FP16 渲染目标（HDR 前置条件）
        val astc: Boolean,            // 是否支持 ASTC 纹理压缩
        val vulkan: Boolean,          // Vulkan 后端可用性
        val metal: Boolean,           // Metal 后端可用性（Android 始终 false）
        val maxMSAA: Int,             // glGetIntegerv(GL_MAX_SAMPLES)
        val geometryShader: Boolean   // 是否支持几何着色器（GLES 3.2+）
    )

    /**
     * 拉取设备能力快照。必须在引擎已 init 之后调用。
     * 失败返回 null（引擎未初始化）。
     */
    fun getDeviceCapabilities(): DeviceCapabilities? {
        handleLock.read {
            if (nativeHandle == 0L) return null
            val a = nativeGetDeviceCapabilities(nativeHandle) ?: return null
            if (a.size < 8) return null
            return DeviceCapabilities(
                glesVersion    = a[0],
                computeShader  = a[1] != 0,
                fp16           = a[2] != 0,
                astc           = a[3] != 0,
                vulkan         = a[4] != 0,
                metal          = a[5] != 0,
                maxMSAA        = a[6],
                geometryShader = a[7] != 0
            )
        }
    }

    /** 禁用 DSR，恢复全分辨率渲染。 */
    fun disableDsr() {
        handleLock.read {
            if (nativeHandle != 0L) nativeDisableDsr(nativeHandle)
        }
    }

    /**
     * 返回最近一次 export 实际应用的 DSR 倍率 [minScale, maxScale]。
     * 若 DSR 深度降低段（如 0.6）表明设备 GPU 性能不足，可用来决策是否降码率。
     */
    fun getDsrScale(): Float {
        handleLock.read {
            return if (nativeHandle != 0L) nativeGetDsrScale(nativeHandle) else 1.0f
        }
    }

    // Native methods
    private external fun nativeUpdateShaderSource(handle: Long, name: String, source: String)
    private external fun nativeGetMetrics(handle: Long): FloatArray?
    private external fun nativeRecordDroppedFrame(handle: Long)
    private external fun nativeMarkFrameRendered(handle: Long, textureId: Int)
    private external fun nativeInit(assetManager: android.content.res.AssetManager): Long
    private external fun nativeRelease(handle: Long)
    private external fun nativeProcessFrame(handle: Long, textureId: Int, width: Int, height: Int, matrix: FloatArray, timestampNs: Long): Int
    private external fun nativeRenderToRecordingSurface(handle: Long, textureId: Int, width: Int, height: Int, timestampNs: Long)
    private external fun nativeSetRecordingSurface(handle: Long, surface: Surface?)
    private external fun nativeUpdateParameterFloat(handle: Long, key: String, value: Float)
    private external fun nativeUpdateParameterInt(handle: Long, key: String, value: Int)
    private external fun nativeUpdateParameterBool(handle: Long, key: String, value: Boolean)
    private external fun nativeAddFilter(handle: Long, filterType: Int)
    private external fun nativeRemoveAllFilters(handle: Long)
    private external fun nativeStartAudioRecord(handle: Long, sampleRate: Int)
    private external fun nativeStopAudioRecord(handle: Long)
    private external fun nativeReadAudioPCM(handle: Long, buffer: ByteArray, length: Int): Int
    private external fun nativeGetAudioTimeNs(handle: Long): Long
    private external fun nativeOnContextLost(handle: Long)
    private external fun nativeOnContextRestored(handle: Long): Int
    private external fun nativeOnTrimMemory(handle: Long, level: Int)
    private external fun nativeSetDsrConfig(handle: Long, targetFps: Float, minScale: Float, maxScale: Float)
    private external fun nativeDisableDsr(handle: Long)
    private external fun nativeGetDsrScale(handle: Long): Float
    private external fun nativeSetBackend(handle: Long, backendType: Int)
    private external fun nativeGetActiveBackend(handle: Long): Int
    /** 从 MediaPipe 接收实时人脸关键点（478×3 = 1434 floats），null 表示无脸 */
    fun updateFaceLandmarks(landmarks: FloatArray?) {
        handleLock.read {
            if (nativeHandle != 0L) {
                nativeUpdateFaceLandmarks(nativeHandle, landmarks)
            }
        }
    }

    private external fun nativeGetGLESVersion(handle: Long): Int
    private external fun nativeGetDeviceCapabilities(handle: Long): IntArray?

    // ── 抖音对标 native 方法 ──────────────────────────────────────────
    private external fun nativePreWarmBeauty(handle: Long): Int
    private external fun nativeEnableBeauty(handle: Long, smooth: Float, whiten: Float): Int
    private external fun nativeDisableBeauty(handle: Long): Int
    private external fun nativeLoadFaceLandmarkModel(handle: Long, modelPath: String): Boolean
    private external fun nativeGetFaceLandmarks(handle: Long): FloatArray?
    private external fun nativeUpdateFaceLandmarks(handle: Long, landmarks: FloatArray?)
    private external fun nativeSetFaceReshape(handle: Long,
        eyeScale: Float, faceSlim: Float, noseSlim: Float,
        foreheadUp: Float, chinV: Float, mouthWidth: Float)
    private external fun nativeSetMakeup(handle: Long, layer: Int,
        r: Float, g: Float, b: Float, intensity: Float)
    private external fun nativeLoadHairModel(handle: Long, modelPath: String): Boolean
    private external fun nativeSetHairColor(handle: Long, r: Float, g: Float, b: Float,
        colorIntensity: Float, glossIntensity: Float)
    private external fun nativeLoadEffect(handle: Long, effectRoot: String): String
    private external fun nativeActivateEffect(handle: Long, effectId: String)
    private external fun nativeDeactivateAllEffects(handle: Long)

    // ── P1 补齐 native 方法 ──────────────────────────────────────────────
    private external fun nativeLoadHandModel(handle: Long, modelPath: String): Boolean
    private external fun nativeGetHandLandmarks(handle: Long): FloatArray?
    private external fun nativeLoadSegModel(handle: Long, modelPath: String): Boolean
    private external fun nativeSetSegMode(handle: Long, mode: Int, bgColorArgb: Int, blurStrength: Float)
    private external fun nativeEnableChromaKey(handle: Long, hueCenter: Float, hueTol: Float, satMin: Float, edgeSoftness: Float)
    private external fun nativeDisableChromaKey(handle: Long)
    private external fun nativeSetFaceMorphStrength(handle: Long, effectIndex: Int, strength: Float)
    private external fun nativeResetFaceMorph(handle: Long)
    private external fun nativeSetBodyEffectStrength(handle: Long, effectIndex: Int, strength: Float)
    private external fun nativeResetBodyEffect(handle: Long)
    private external fun nativeUpdateBodyPose(handle: Long, poseData: FloatArray)
    private external fun nativeGetExpressions(handle: Long): FloatArray?

    // ── 手部关键点 ────────────────────────────────────────────────────────

    /**
     * 加载手部关键点模型（MediaPipe Hands .tflite）。
     * @param modelPath  assets 解压后绝对路径
     */
    fun loadHandModel(modelPath: String): Boolean {
        handleLock.read {
            return if (nativeHandle != 0L) nativeLoadHandModel(nativeHandle, modelPath) else false
        }
    }

    /**
     * 获取最新帧手部关键点。
     * @return FloatArray[1 + 2*21*3]，[0]=handCount；每只手 21点×(x,y,z)；无手时返回 null
     */
    fun getHandLandmarks(): FloatArray? {
        handleLock.read {
            return if (nativeHandle != 0L) nativeGetHandLandmarks(nativeHandle) else null
        }
    }

    // ── 人像/人体分割 ─────────────────────────────────────────────────────

    /**
     * 加载人像分割模型（selfie_segmentation .tflite）。
     */
    fun loadSegmentationModel(modelPath: String): Boolean {
        handleLock.read {
            return if (nativeHandle != 0L) nativeLoadSegModel(nativeHandle, modelPath) else false
        }
    }

    /**
     * 设置分割背景处理模式。
     * @param mode          0=模糊背景 1=纯色背景 2=透明 3=图片背景
     * @param bgColorArgb   REPLACE_BG 模式的背景色（ARGB int）
     * @param blurStrength  BLUR_BG 模式的模糊半径
     */
    fun setSegmentationMode(mode: Int, bgColorArgb: Int = 0xFF000000.toInt(), blurStrength: Float = 15f) {
        handleLock.read {
            if (nativeHandle != 0L) nativeSetSegMode(nativeHandle, mode, bgColorArgb, blurStrength)
        }
    }

    // ── 绿幕/色度键 ───────────────────────────────────────────────────────

    /**
     * 启用绿幕抠像滤镜。
     * @param hueCenter   色相中心 [0,1]，绿≈0.333，蓝≈0.667
     * @param hueTol      色相容差（单侧）[0,0.5]
     * @param satMin      饱和度下限 [0,1]
     * @param edgeSoftness 边缘羽化 [0,1]
     */
    fun enableChromaKey(
        hueCenter: Float = 0.333f,
        hueTol: Float = 0.10f,
        satMin: Float = 0.25f,
        edgeSoftness: Float = 0.06f
    ) {
        handleLock.read {
            if (nativeHandle != 0L)
                nativeEnableChromaKey(nativeHandle, hueCenter, hueTol, satMin, edgeSoftness)
        }
    }

    /** 关闭绿幕抠像，恢复原始渲染管线。 */
    fun disableChromaKey() {
        handleLock.read {
            if (nativeHandle != 0L) nativeDisableChromaKey(nativeHandle)
        }
    }

    // ── 人脸形变 ──────────────────────────────────────────────────────────

    /**
     * 设置人脸形变强度（须先加载人脸关键点模型）。
     * @param effectIndex  0=瘦脸 1=大眼 2=瘦下颌 3=额头 4=瘦鼻 5=嘴型 6=眼距 7=下巴
     * @param strength     强度 [0,1]
     */
    fun setFaceMorphStrength(effectIndex: Int, strength: Float) {
        handleLock.read {
            if (nativeHandle != 0L) nativeSetFaceMorphStrength(nativeHandle, effectIndex, strength)
        }
    }

    /** 重置所有人脸形变强度为 0。 */
    fun resetFaceMorph() {
        handleLock.read {
            if (nativeHandle != 0L) nativeResetFaceMorph(nativeHandle)
        }
    }

    // ── 身体特效 ──────────────────────────────────────────────────────────

    /**
     * 设置身体特效强度。
     * @param effectIndex  0=瘦身 1=长腿 2=小头 3=窄肩 4=提臀
     * @param strength     强度 [0,1]
     */
    fun setBodyEffectStrength(effectIndex: Int, strength: Float) {
        handleLock.read {
            if (nativeHandle != 0L) nativeSetBodyEffectStrength(nativeHandle, effectIndex, strength)
        }
    }

    /** 重置所有身体特效强度为 0。 */
    fun resetBodyEffect() {
        handleLock.read {
            if (nativeHandle != 0L) nativeResetBodyEffect(nativeHandle)
        }
    }

    /**
     * 更新身体骨骼点数据（由 BodyPoseDetector 提供后调用）。
     * @param poseData  FloatArray[17*3]，每组 (x, y, confidence)，COCO 17点格式
     */
    fun updateBodyPose(poseData: FloatArray) {
        handleLock.read {
            if (nativeHandle != 0L) nativeUpdateBodyPose(nativeHandle, poseData)
        }
    }

    // ── 表情检测 ──────────────────────────────────────────────────────────

    /**
     * 获取当前帧表情检测结果（基于已加载的人脸关键点，无需额外模型）。
     * @return FloatArray[12] 或 null（未检测到人脸）：
     *   [0]=smiling [1]=mouthOpen [2]=blinkLeft [3]=blinkRight
     *   [4]=browRaise [5]=winkLeft [6]=winkRight（以上 0/1）
     *   [7]=smileIntensity [8]=mouthOpenness [9]=leftEyeOpenness
     *   [10]=rightEyeOpenness [11]=browRaiseAmount（以上 [0,1]）
     */
    fun getExpressions(): FloatArray? {
        handleLock.read {
            return if (nativeHandle != 0L) nativeGetExpressions(nativeHandle) else null
        }
    }

    fun recreateOESTexture() {
        surfaceTexture?.release()
        surfaceTexture = null
        if (oesTextureId != -1) {
            oesTextureId = -1
        }

        val textures = IntArray(1)
        GLES20.glGenTextures(1, textures, 0)
        oesTextureId = textures[0]

        GLES20.glBindTexture(0x8D65, oesTextureId) // GL_TEXTURE_EXTERNAL_OES
        // [FIX] GL_LINEAR for MIN_FILTER — see init() comment
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glBindTexture(0x8D65, 0)

        surfaceTexture = SurfaceTexture(oesTextureId)
        surfaceTexture?.setOnFrameAvailableListener(this)
    }
}
