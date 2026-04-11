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
    var onRenderErrorListener: ((errorCode: Int, errorMessage: String) -> Unit)? = null

    @androidx.annotation.Keep
    private fun onNativeRenderError(errorCode: Int, errorMessage: String) {
        onRenderErrorListener?.invoke(errorCode, errorMessage)
    }

    // Call on GL thread to initialize

    fun updateShaderSource(name: String, source: String): Int {
        if (nativeHandle != 0L) {
            try {
                nativeUpdateShaderSource(nativeHandle, name, source)
                return 0
            } catch (e: NativeRenderException) {
                return e.errorCode
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
        try {
            nativeHandle = nativeInit(assetManager)
        } catch (e: NativeRenderException) {
            return e.errorCode
        }
        if (nativeHandle == 0L) return -1001 // ERR_INIT_CONTEXT_FAILED

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

        // 如果返回值是负数，表示产生了错误 (onNativeRenderError is called from C++)
        if (outputTexId >= 0) {
            onFrameProcessedListener?.invoke(outputTexId)
            if (lastFrameTimeMs > 0) {
                onPerformanceUpdateListener?.invoke(lastFrameTimeMs)
            }
        }
    }

    // Recording API
    fun startRecording(surface: Surface): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        return try {
            nativeSetRecordingSurface(nativeHandle, surface)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
    }

    fun stopRecording(): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        return try {
            nativeSetRecordingSurface(nativeHandle, null)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
    }

    // Parameters
    fun setFlip(horizontal: Boolean, vertical: Boolean): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        return try {
            nativeUpdateParameterBool(nativeHandle, "flipHorizontal", horizontal)
            nativeUpdateParameterBool(nativeHandle, "flipVertical", vertical)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
    }

    fun updateParameterFloat(key: String, value: Float): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        return try {
            nativeUpdateParameterFloat(nativeHandle, key, value)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
    }

    fun updateParameterInt(key: String, value: Int): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        return try {
            nativeUpdateParameterInt(nativeHandle, key, value)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
    }

    // Pipeline
    fun addFilter(type: Int): Result<Unit> {
        return try {
            nativeAddFilter(nativeHandle, type)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
    }

    fun removeAllFilters(): Result<Unit> {
        return try {
            nativeRemoveAllFilters(nativeHandle)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
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
        return try {
            nativeStartAudioRecord(nativeHandle, sampleRate)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
    }

    fun stopAudioRecord(): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(VideoSdkError.InvalidOperation("Engine not initialized"))
        return try {
            nativeStopAudioRecord(nativeHandle)
            Result.success(Unit)
        } catch (e: NativeRenderException) {
            Result.failure(VideoSdkError.NativeError(e.errorCode, e.message ?: "Unknown native error"))
        }
    }

    fun readAudioPCM(buffer: ByteArray, length: Int): Int {
        return if (nativeHandle != 0L) nativeReadAudioPCM(nativeHandle, buffer, length) else 0
    }

    // Native methods
    private external fun nativeUpdateShaderSource(handle: Long, name: String, source: String)
    private external fun nativeGetMetrics(handle: Long): FloatArray?
    private external fun nativeRecordDroppedFrame(handle: Long)
    private external fun nativeInit(assetManager: android.content.res.AssetManager): Long
    private external fun nativeRelease(handle: Long)
    private external fun nativeProcessFrame(handle: Long, textureId: Int, width: Int, height: Int, matrix: FloatArray, timestampNs: Long): Int
    private external fun nativeSetRecordingSurface(handle: Long, surface: Surface?)
    private external fun nativeUpdateParameterFloat(handle: Long, key: String, value: Float)
    private external fun nativeUpdateParameterInt(handle: Long, key: String, value: Int)
    private external fun nativeUpdateParameterBool(handle: Long, key: String, value: Boolean)
    private external fun nativeAddFilter(handle: Long, filterType: Int)
    private external fun nativeRemoveAllFilters(handle: Long)
    private external fun nativeStartAudioRecord(handle: Long, sampleRate: Int)
    private external fun nativeStopAudioRecord(handle: Long)
    private external fun nativeReadAudioPCM(handle: Long, buffer: ByteArray, length: Int): Int
}
