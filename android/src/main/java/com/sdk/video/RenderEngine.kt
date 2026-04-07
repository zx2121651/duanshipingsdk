package com.sdk.video

import android.graphics.SurfaceTexture
import android.opengl.GLES20
import android.view.Surface

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
    }

    private var nativeHandle: Long = 0
    private var surfaceTexture: SurfaceTexture? = null
    private var oesTextureId: Int = -1
    private val transformMatrix = FloatArray(16)

    // Performance field updated via JNI reflection
    @JvmField
    var lastFrameTimeMs: Long = 0L

    var onFrameProcessedListener: ((outputTextureId: Int) -> Unit)? = null
    var onPerformanceUpdateListener: ((durationMs: Long) -> Unit)? = null
    var onRenderErrorListener: ((errorCode: Int) -> Unit)? = null

    // Call on GL thread to initialize

    fun updateShaderSource(name: String, source: String): Int {
        if (nativeHandle != 0L) {
            return nativeUpdateShaderSource(nativeHandle, name, source)
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
        if (nativeHandle == 0L) return null
        val arr = nativeGetMetrics(nativeHandle) ?: return null
        return PerformanceMetrics(arr[0], arr[1], arr[2], arr[3], arr[4].toInt())
    }

    fun recordDroppedFrame() {
        if (nativeHandle != 0L) {
            nativeRecordDroppedFrame(nativeHandle)
        }
    }
fun init(assetManager: android.content.res.AssetManager): Int {
        nativeHandle = nativeInit(assetManager)
        if (nativeHandle == 0L) return -1

        val textures = IntArray(1)
        GLES20.glGenTextures(1, textures, 0)
        oesTextureId = textures[0]

        GLES20.glBindTexture(0x8D65, oesTextureId) // GL_TEXTURE_EXTERNAL_OES
        GLES20.glTexParameteri(0x8D65, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST)
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
        if (nativeHandle == 0L || st == null) return

        st.updateTexImage()
        st.getTransformMatrix(transformMatrix)

        // Pass timestamp to native layer for MediaCodec EGL presentation time
        val timestampNs = st.timestamp

        val outputTexId = nativeProcessFrame(nativeHandle, oesTextureId, width, height, transformMatrix, timestampNs)

        // 如果返回值是负数，表示产生了错误
        if (outputTexId < 0) {
            onRenderErrorListener?.invoke(outputTexId)
        } else {
            onFrameProcessedListener?.invoke(outputTexId)
            if (lastFrameTimeMs > 0) {
                onPerformanceUpdateListener?.invoke(lastFrameTimeMs)
            }
        }
    }

    // Recording API
    fun startRecording(surface: Surface): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        val code = nativeSetRecordingSurface(nativeHandle, surface)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    fun stopRecording(): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        val code = nativeSetRecordingSurface(nativeHandle, null)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    // Parameters
    fun setFlip(horizontal: Boolean, vertical: Boolean): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        var code = nativeUpdateParameterBool(nativeHandle, "flipHorizontal", horizontal)
        if (code != 0) return Result.failure(VideoSdkError.fromNativeCode(code))
        code = nativeUpdateParameterBool(nativeHandle, "flipVertical", vertical)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    fun updateParameterFloat(key: String, value: Float): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        val code = nativeUpdateParameterFloat(nativeHandle, key, value)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    fun updateParameterInt(key: String, value: Int): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        val code = nativeUpdateParameterInt(nativeHandle, key, value)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    // Pipeline
    fun addFilter(type: Int): Result<Unit> {
        val code = nativeAddFilter(nativeHandle, type)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    fun removeAllFilters(): Result<Unit> {
        val code = nativeRemoveAllFilters(nativeHandle)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    // Call on GL thread to release
    fun release() {
        if (nativeHandle != 0L) {
            nativeRelease(nativeHandle)
            nativeHandle = 0
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
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        val code = nativeStartAudioRecord(nativeHandle, sampleRate)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    fun stopAudioRecord(): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        val code = nativeStopAudioRecord(nativeHandle)
        return if (code == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(code))
    }

    fun readAudioPCM(buffer: ByteArray, length: Int): Int {
        return if (nativeHandle != 0L) nativeReadAudioPCM(nativeHandle, buffer, length) else 0
    }

    // Native methods
    private external fun nativeUpdateShaderSource(handle: Long, name: String, source: String): Int

    private external fun nativeGetMetrics(handle: Long): FloatArray?
    private external fun nativeRecordDroppedFrame(handle: Long)
private external fun nativeInit(assetManager: android.content.res.AssetManager): Long
    private external fun nativeRelease(handle: Long)
    private external fun nativeProcessFrame(handle: Long, textureId: Int, width: Int, height: Int, matrix: FloatArray, timestampNs: Long): Int
    private external fun nativeSetRecordingSurface(handle: Long, surface: Surface?): Int
    private external fun nativeUpdateParameterFloat(handle: Long, key: String, value: Float): Int
    private external fun nativeUpdateParameterInt(handle: Long, key: String, value: Int): Int
    private external fun nativeUpdateParameterBool(handle: Long, key: String, value: Boolean): Int
    private external fun nativeAddFilter(handle: Long, filterType: Int): Int
    private external fun nativeRemoveAllFilters(handle: Long): Int
    private external fun nativeStartAudioRecord(handle: Long, sampleRate: Int): Int
    private external fun nativeStopAudioRecord(handle: Long): Int
    private external fun nativeReadAudioPCM(handle: Long, buffer: ByteArray, length: Int): Int
}
