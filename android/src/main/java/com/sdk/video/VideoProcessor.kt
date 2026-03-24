package com.sdk.video

import android.content.Context
import android.graphics.SurfaceTexture

class VideoProcessor {

    companion object {
        init {
            System.loadLibrary("video-sdk")
        }

        const val FILTER_TYPE_BRIGHTNESS = 0
        const val FILTER_TYPE_GAUSSIAN_BLUR = 1
        const val FILTER_TYPE_LOOKUP = 2
    }

    private var nativeHandle: Long = 0

    init {
        nativeHandle = nativeCreate()
    }

    // Call on GL thread
    fun initialize(context: Context) {
        nativeInitialize(nativeHandle)
    }

    // Usually called from SurfaceTexture.OnFrameAvailableListener on GL thread
    // textureIn: OES texture id
    // transformMatrix: float array of size 16 from surfaceTexture.getTransformMatrix()
    fun processFrame(textureIn: Int, width: Int, height: Int, transformMatrix: FloatArray): Int {
        if (transformMatrix.size != 16) {
            throw IllegalArgumentException("Transform matrix must be a 4x4 matrix (16 floats)")
        }
        return nativeProcessFrame(nativeHandle, textureIn, width, height, transformMatrix)
    }

    // Update float filter parameter
    fun updateParameterFloat(key: String, value: Float) {
        nativeUpdateParameterFloat(nativeHandle, key, value)
    }

    // Update int filter parameter (e.g. LUT texture ID)
    fun updateParameterInt(key: String, value: Int) {
        nativeUpdateParameterInt(nativeHandle, key, value)
    }

    // Add filter to pipeline
    fun addFilter(type: Int) {
        nativeAddFilter(nativeHandle, type)
    }

    // Clear pipeline
    fun removeAllFilters() {
        nativeRemoveAllFilters(nativeHandle)
    }

    // Call on GL thread
    fun release() {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0
        }
    }

    // Native methods
    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeInitialize(handle: Long)
    private external fun nativeProcessFrame(handle: Long, textureId: Int, width: Int, height: Int, matrix: FloatArray): Int
    private external fun nativeUpdateParameterFloat(handle: Long, key: String, value: Float)
    private external fun nativeUpdateParameterInt(handle: Long, key: String, value: Int)
    private external fun nativeAddFilter(handle: Long, filterType: Int)
    private external fun nativeRemoveAllFilters(handle: Long)
}
