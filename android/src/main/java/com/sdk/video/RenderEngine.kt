package com.sdk.video

import android.graphics.SurfaceTexture
import android.opengl.GLES20

class RenderEngine(private val width: Int, private val height: Int) : SurfaceTexture.OnFrameAvailableListener {

    companion object {
        init {
            System.loadLibrary("video-sdk")
        }

        const val FILTER_TYPE_BRIGHTNESS = 0
        const val FILTER_TYPE_GAUSSIAN_BLUR = 1
        const val FILTER_TYPE_LOOKUP = 2
    }

    private var nativeHandle: Long = 0
    private var surfaceTexture: SurfaceTexture? = null
    private var oesTextureId: Int = -1
    private val transformMatrix = FloatArray(16)

    // Callback for when a frame finishes processing
    var onFrameProcessedListener: ((outputTextureId: Int) -> Unit)? = null

    // Call on GL thread to initialize
    fun init() {
        nativeHandle = nativeInit()

        // Generate an OES texture ID to bind to SurfaceTexture
        val textures = IntArray(1)
        GLES20.glGenTextures(1, textures, 0)
        oesTextureId = textures[0]

        // Ensure texture is bound correctly as OES
        GLES20.glBindTexture(0x8D65, oesTextureId) // 0x8D65 is GL_TEXTURE_EXTERNAL_OES
        GLES20.glTexParameterf(0x8D65, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST.toFloat())
        GLES20.glTexParameterf(0x8D65, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR.toFloat())
        GLES20.glTexParameterf(0x8D65, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE.toFloat())
        GLES20.glTexParameterf(0x8D65, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE.toFloat())
        GLES20.glBindTexture(0x8D65, 0)

        surfaceTexture = SurfaceTexture(oesTextureId)
        surfaceTexture?.setOnFrameAvailableListener(this)
    }

    // Returns the SurfaceTexture to be fed into CameraX or ExoPlayer
    fun getSurfaceTexture(): SurfaceTexture? {
        return surfaceTexture
    }

    override fun onFrameAvailable(st: SurfaceTexture?) {
        if (nativeHandle == 0L || st == null) return

        // Update texture image from camera stream to our OES texture
        st.updateTexImage()

        // Get the texture transformation matrix that corrects rotation/cropping
        st.getTransformMatrix(transformMatrix)

        // Run the native C++ pipeline
        val outputTexId = nativeProcessFrame(nativeHandle, oesTextureId, width, height, transformMatrix)

        // Notify listener
        onFrameProcessedListener?.invoke(outputTexId)
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
    fun addFilter(type: Int) {
        nativeAddFilter(nativeHandle, type)
    }

    fun removeAllFilters() {
        nativeRemoveAllFilters(nativeHandle)
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

    // Native methods
    private external fun nativeInit(): Long
    private external fun nativeRelease(handle: Long)
    private external fun nativeProcessFrame(handle: Long, textureId: Int, width: Int, height: Int, matrix: FloatArray): Int
    private external fun nativeUpdateParameterFloat(handle: Long, key: String, value: Float)
    private external fun nativeUpdateParameterInt(handle: Long, key: String, value: Int)
    private external fun nativeUpdateParameterBool(handle: Long, key: String, value: Boolean)
    private external fun nativeAddFilter(handle: Long, filterType: Int)
    private external fun nativeRemoveAllFilters(handle: Long)
}
