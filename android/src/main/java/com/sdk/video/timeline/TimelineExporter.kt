package com.sdk.video.timeline

import androidx.annotation.Keep
import com.sdk.video.InternalApi
import com.sdk.video.RenderEngine
import com.sdk.video.VideoExportConfig
import com.sdk.video.VideoSdkError

/**
 * Android Exporter Facade
 */
@OptIn(InternalApi::class)
class TimelineExporter {
    private var nativeHandle: Long = 0

    init {
        nativeHandle = nativeCreate()
    }

    fun configure(config: VideoExportConfig): Int {
        return nativeConfigure(nativeHandle, config.outputPath, config.width, config.height, config.fps, config.videoBitrate)
    }

    fun exportAsync(
        timeline: TimelineManager,
        renderEngine: RenderEngine,
        onProgress: (Float) -> Unit,
        onComplete: (Int, String) -> Unit
    ): Int {
        return nativeExportAsync(
            nativeHandle,
            timeline.getNativeHandle(),
            renderEngine.getNativeHandle(),
            object : ProgressCallback {
                @Keep
                override fun onProgress(progress: Float) {
                    onProgress(progress)
                }
            },
            object : CompletionCallback {
                @Keep
                override fun onComplete(errorCode: Int, message: String) {
                    onComplete(errorCode, message)
                }
            }
        )
    }

    fun cancel() {
        nativeCancel(nativeHandle)
    }

    fun release() {
        if (nativeHandle != 0L) {
            nativeRelease(nativeHandle)
            nativeHandle = 0
        }
    }

    fun getState(): Int = nativeGetState(nativeHandle)
    fun getProgress(): Float = nativeGetProgress(nativeHandle)

    // JNI Interfaces
    private interface ProgressCallback {
        fun onProgress(progress: Float)
    }

    private interface CompletionCallback {
        fun onComplete(errorCode: Int, message: String)
    }

    private external fun nativeCreate(): Long
    private external fun nativeRelease(handle: Long)
    private external fun nativeConfigure(handle: Long, outputPath: String, width: Int, height: Int, fps: Int, bitrate: Int): Int
    private external fun nativeExportAsync(handle: Long, timelineHandle: Long, engineHandle: Long, onProgress: ProgressCallback, onComplete: CompletionCallback): Int
    private external fun nativeCancel(handle: Long)
    private external fun nativeGetState(handle: Long): Int
    private external fun nativeGetProgress(handle: Long): Float

    companion object {
        init {
            System.loadLibrary("video-sdk")
        }
    }
}
