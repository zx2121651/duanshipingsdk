package com.sdk.video.timeline

import com.sdk.video.InternalApi

/**
 * TimelinePreviewBridge — JNI façade for timeline frame rendering.
 *
 * All methods **must** be called on the same GL thread (GLSurfaceView's renderer thread).
 *
 * Lifecycle:
 * ```
 * onSurfaceCreated  → initOnGLThread(timelineHandle, w, h)  → returns false if native init fails
 * onSurfaceChanged  → surfaceChanged(w, h)
 * onDrawFrame       → renderFrame(positionNs)
 * onSurfaceDestroyed → releaseOnGLThread()
 * ```
 */
@InternalApi
class TimelinePreviewBridge {

    private var nativeHandle: Long = 0L

    /**
     * Initialises the native preview context. Must be on the GL thread.
     * @param timelineHandle  Raw handle from [TimelineManager.getNativeHandle]
     * @param width           Surface width in pixels
     * @param height          Surface height in pixels
     * @return `true` if the context was created successfully.
     */
    fun initOnGLThread(timelineHandle: Long, width: Int, height: Int): Boolean {
        if (nativeHandle != 0L) releaseOnGLThread()
        nativeHandle = nativeCreate(timelineHandle, width, height)
        return nativeHandle != 0L
    }

    /**
     * Notify the native layer that the surface dimensions changed.
     * Must be on the GL thread.
     */
    fun surfaceChanged(width: Int, height: Int) {
        if (nativeHandle != 0L) nativeSurfaceChanged(nativeHandle, width, height)
    }

    /**
     * Render one frame at [positionNs] to the GLSurfaceView default framebuffer.
     * Must be on the GL thread.
     */
    fun renderFrame(positionNs: Long) {
        if (nativeHandle != 0L) nativeRenderFrame(nativeHandle, positionNs)
    }

    /**
     * Frame-accurate seek: flushes decoder buffers and renders the exact frame at [positionNs].
     * Slower than [renderFrame] — use during scrub-end or jump-to-position.
     * Must be on the GL thread.
     */
    fun seekTo(positionNs: Long) {
        if (nativeHandle != 0L) nativeSeekTo(nativeHandle, positionNs)
    }

    /**
     * Fast I-frame-approximate scrub: suitable for continuous drag on the timeline ruler.
     * Renders the nearest sync frame; very low latency but not frame-exact.
     * Must be on the GL thread.
     */
    fun scrubTo(positionNs: Long) {
        if (nativeHandle != 0L) nativeScrubTo(nativeHandle, positionNs)
    }

    /**
     * Releases all native GL resources. Must be on the GL thread.
     */
    fun releaseOnGLThread() {
        if (nativeHandle != 0L) {
            nativeRelease(nativeHandle)
            nativeHandle = 0L
        }
    }

    val isInitialized: Boolean get() = nativeHandle != 0L

    /**
     * Register a video clip so the native [DecoderPool] can decode its frames.
     * Thread-safe — may be called from any thread after [initOnGLThread] returns `true`.
     *
     * @param clipId     Must match the id used in [TimelineManager.addClip]
     * @param sourcePath Absolute file path or `content://` URI string
     */
    fun registerClip(clipId: String, sourcePath: String) {
        if (nativeHandle != 0L) nativeRegisterClip(nativeHandle, clipId, sourcePath)
    }

    // ── native declarations ────────────────────────────────────────────
    private external fun nativeCreate(timelineHandle: Long, width: Int, height: Int): Long
    private external fun nativeSurfaceChanged(handle: Long, width: Int, height: Int)
    private external fun nativeRenderFrame(handle: Long, positionNs: Long)
    private external fun nativeSeekTo(handle: Long, positionNs: Long)
    private external fun nativeScrubTo(handle: Long, positionNs: Long)
    private external fun nativeRegisterClip(handle: Long, clipId: String, sourcePath: String)
    private external fun nativeRelease(handle: Long)

    companion object {
        init { System.loadLibrary("video-sdk") }
    }
}
