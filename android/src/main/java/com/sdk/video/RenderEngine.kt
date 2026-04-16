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

    var onFrameProcessedListener: ((outputTextureId: Int) -> Unit)? = null
    var onPerformanceUpdateListener: ((durationMs: Long) -> Unit)? = null
    var onRenderErrorListener: ((errorCode: Int, errorMessage: String) -> Unit)? = null

    // 确保被 R8 混淆器保留，供 C++ 回调
    @androidx.annotation.Keep
    private fun onNativeRenderError(errorCode: Int, errorMessage: String) {
        android.util.Log.e("RenderEngine", "FATAL NATIVE ERROR [$errorCode]: $errorMessage")
        // 将故障讯号向外转发给负责业务逻辑的 Facade
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
        if (st == null) return

        handleLock.read {
            if (nativeHandle == 0L) return

            st.updateTexImage()
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
                timestampNs = recordingStartTimeNs + (timestampNs - firstFrameSessionTimestamp)

                if (lastVideoPtsNs != -1L && timestampNs <= lastVideoPtsNs) {
                    timestampNs = lastVideoPtsNs + 10000 // Ensure strict monotonicity (10us)
                }
                lastVideoPtsNs = timestampNs
            }

            val outputTexId = nativeProcessFrame(nativeHandle, oesTextureId, width, height, transformMatrix, timestampNs)

            // 如果返回值是负数，表示产生了错误
            if (outputTexId >= 0) {
                onFrameProcessedListener?.invoke(outputTexId)
                if (lastFrameTimeMs > 0) {
                    onPerformanceUpdateListener?.invoke(lastFrameTimeMs)
                }
            } else {
                // Defensive fallback: if native code returned an error but didn't call onNativeRenderError callback
                // (e.g., due to JNI reference issues), we manually notify the listener.
                onRenderErrorListener?.invoke(outputTexId, "Native frame processing failed with code: $outputTexId")
            }
            Unit
        }
    }

    // Recording API
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

    // Call on GL thread to release
    fun release() {
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
    private external fun nativeGetAudioTimeNs(handle: Long): Long
}
