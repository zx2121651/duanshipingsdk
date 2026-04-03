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

    // Call on GL thread to initialize
    fun init(): Int {
        nativeHandle = nativeInit()
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

        onFrameProcessedListener?.invoke(outputTexId)
        onPerformanceUpdateListener?.invoke(lastFrameTimeMs)
    }

    // Recording API
    fun startRecording(surface: Surface) {
        nativeSetRecordingSurface(nativeHandle, surface)
    }

    fun stopRecording() {
        nativeSetRecordingSurface(nativeHandle, null)
    }

    // Parameters
    fun setFlip(horizontal: Boolean, vertical: Boolean) {
        nativeUpdateParameterBool(nativeHandle, "flipHorizontal", horizontal)
        nativeUpdateParameterBool(nativeHandle, "flipVertical", vertical)
    }

    fun updateParameterFloat(key: String, value: Float) {
        nativeUpdateParameterFloat(nativeHandle, key, value)
    }

    fun updateParameterInt(key: String, value: Int) {
        nativeUpdateParameterInt(nativeHandle, key, value)
    }

    // Pipeline
    fun addFilter(type: Int): Result<Unit> {
        val code = nativeAddFilter(nativeHandle, type)
        return if (code == 0) Result.success(Unit) else Result.failure(Exception("Native addFilter failed with code $code"))
    }

    fun removeAllFilters(): Result<Unit> {
        val code = nativeRemoveAllFilters(nativeHandle)
        return if (code == 0) Result.success(Unit) else Result.failure(Exception("Native removeAllFilters failed with code $code"))
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
        if (nativeHandle == 0L) return Result.failure(Exception("Engine not initialized"))
        val code = nativeStartAudioRecord(nativeHandle, sampleRate)
        return if (code == 0) Result.success(Unit) else Result.failure(Exception("Native startAudioRecord failed with code $code"))
    }

    fun stopAudioRecord(): Result<Unit> {
        if (nativeHandle == 0L) return Result.failure(Exception("Engine not initialized"))
        val code = nativeStopAudioRecord(nativeHandle)
        return if (code == 0) Result.success(Unit) else Result.failure(Exception("Native stopAudioRecord failed with code $code"))
    }

    fun readAudioPCM(buffer: ByteArray, length: Int): Int {
        return if (nativeHandle != 0L) nativeReadAudioPCM(nativeHandle, buffer, length) else 0
    }

    // Native methods
    private external fun nativeInit(): Long
    private external fun nativeRelease(handle: Long)
    private external fun nativeProcessFrame(handle: Long, textureId: Int, width: Int, height: Int, matrix: FloatArray, timestampNs: Long): Int
    private external fun nativeSetRecordingSurface(handle: Long, surface: Surface?)
    private external fun nativeUpdateParameterFloat(handle: Long, key: String, value: Float)
    private external fun nativeUpdateParameterInt(handle: Long, key: String, value: Int)
    private external fun nativeUpdateParameterBool(handle: Long, key: String, value: Boolean)
    private external fun nativeAddFilter(handle: Long, filterType: Int): Int
    private external fun nativeRemoveAllFilters(handle: Long): Int
    private external fun nativeStartAudioRecord(handle: Long, sampleRate: Int): Int
    private external fun nativeStopAudioRecord(handle: Long): Int
    private external fun nativeReadAudioPCM(handle: Long, buffer: ByteArray, length: Int): Int
}
