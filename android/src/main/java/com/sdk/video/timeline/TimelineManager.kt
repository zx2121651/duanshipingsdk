package com.sdk.video.timeline

import com.sdk.video.InternalApi

/**
 * 真实的非线性剪辑引擎门面类 (NLE Timeline Facade)
 * 这里管理的数据模型是剪辑业务层的来源，它将驱动底层的 Decoder 和 RenderEngine
 */
@OptIn(InternalApi::class)
class TimelineManager(val outputWidth: Int, val outputHeight: Int, val fps: Int) {

    // 原生 C++ Timeline 对象的指针
    private var nativeHandle: Long = 0

    init {
        System.loadLibrary("video-sdk")
        nativeHandle = nativeCreateTimeline(outputWidth, height = outputHeight, fps = fps)
    }

    enum class TrackType(val value: Int) {
        MAIN_VIDEO(0), PIP_VIDEO(1), AUDIO_ONLY(2)
    }

    enum class MediaType(val value: Int) {
        VIDEO(0), AUDIO(1), IMAGE(2)
    }

    // 在时间线上新建一条轨道（如同 PS 新建图层）
    fun addTrack(zIndex: Int, trackType: TrackType): Int {
        return nativeAddTrack(nativeHandle, zIndex, trackType.value)
    }

    // 将用户相册里的一段视频/图片/音乐“放置”到指定的轨道时间区间上
    fun addClip(
        zIndex: Int, clipId: String, sourcePath: String, mediaType: MediaType,
        trimInUs: Long, trimOutUs: Long, timelineInUs: Long
    ): Int {
        return nativeAddClip(nativeHandle, zIndex, clipId, sourcePath, mediaType.value, trimInUs, trimOutUs, timelineInUs)
    }

    // 销毁 C++ 的时间线模型

    enum class TransitionType(val value: Int) {
        NONE(0), CROSSFADE(1), WIPE_LEFT(2)
    }

    fun setClipSpeed(zIndex: Int, clipId: String, speed: Float): Int {
        return nativeSetClipSpeed(nativeHandle, zIndex, clipId, speed)
    }

    fun setClipTransition(zIndex: Int, clipId: String, type: TransitionType, durationUs: Long): Int {
        return nativeSetClipTransition(nativeHandle, zIndex, clipId, type.value, durationUs)
    }

    fun addClipKeyframe(zIndex: Int, clipId: String, property: String, relativeTimeUs: Long, value: Float): Int {
        return nativeAddClipKeyframe(nativeHandle, zIndex, clipId, property, relativeTimeUs, value)
    }

    fun removeClip(zIndex: Int, clipId: String): Int {
        return nativeRemoveClip(nativeHandle, zIndex, clipId)
    }

    fun getNativeHandle(): Long = nativeHandle

    fun release() {
        if (nativeHandle != 0L) {
            nativeReleaseTimeline(nativeHandle)
            nativeHandle = 0L
        }
    }

    // --- 底层 JNI 声明 ---
    @InternalApi
    private external fun nativeCreateTimeline(width: Int, height: Int, fps: Int): Long
    @InternalApi
    private external fun nativeReleaseTimeline(handle: Long)
    @InternalApi
    private external fun nativeAddTrack(handle: Long, zIndex: Int, trackType: Int): Int
    @InternalApi
    private external fun nativeAddClip(
        handle: Long, zIndex: Int, clipId: String, sourcePath: String, mediaType: Int,
        trimInUs: Long, trimOutUs: Long, timelineInUs: Long
    ): Int

    private external fun nativeSetClipSpeed(handle: Long, zIndex: Int, clipId: String, speed: Float): Int
    private external fun nativeSetClipTransition(handle: Long, zIndex: Int, clipId: String, type: Int, durationUs: Long): Int
    private external fun nativeAddClipKeyframe(handle: Long, zIndex: Int, clipId: String, property: String, relativeTimeUs: Long, value: Float): Int
    private external fun nativeRemoveClip(handle: Long, zIndex: Int, clipId: String): Int
}
