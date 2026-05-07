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

    /**
     * 解码器软解 Fallback 策略（对应 C++ DecoderFallbackStrategy 枚举）
     *
     * - [AUTO]      — 硬件解码失败后自动切换 FFmpeg 软解（默认）
     * - [HW_ONLY]   — 仅硬件解码，失败则返回错误帧（用于高质量导出）
     * - [SW_FIRST]  — 优先软解（适合低端机 / 模拟器 / 特殊编码格式）
     */
    enum class DecoderFallbackStrategy(val value: Int) {
        AUTO(0), HW_ONLY(1), SW_FIRST(2)
    }

    /**
     * 设置解码器 Fallback 策略。
     *
     * 需要在调用 [addClip] 之前设置以生效。
     * 若 SDK 未集成 FFmpeg（[isSoftwareDecoderAvailable] 返回 false），
     * AUTO 与 HW_ONLY 行为一致。
     *
     * @param strategy  目标策略
     * @param decoderPoolHandle  DecoderPool 原生指针（可由 NativeBridge 获取）
     */
    fun setDecoderFallbackStrategy(strategy: DecoderFallbackStrategy, decoderPoolHandle: Long) {
        if (decoderPoolHandle != 0L) {
            nativeSetDecoderFallbackStrategy(decoderPoolHandle, strategy.value)
        }
    }

    /**
     * 运行时查询 FFmpeg 软解是否已编译集成。
     *
     * 返回 true 时 [DecoderFallbackStrategy.AUTO] 和 [DecoderFallbackStrategy.SW_FIRST] 生效；
     * 返回 false 时软解路径返回 ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED。
     */
    fun isSoftwareDecoderAvailable(): Boolean = nativeIsSoftwareDecoderAvailable()

    // =========================================================================
    // AI 美颜 / 人像分割 API
    // =========================================================================

    /**
     * 人像分割模式（对应 SegmentationFilter u_mode）
     *
     * - [BLUR_BG]    — 背景模糊，前景保留原图
     * - [REPLACE_BG] — 背景替换为指定纯色
     */
    enum class SegmentationMode(val value: Int) {
        BLUR_BG(0), REPLACE_BG(1)
    }

    /**
     * 加载 AI 推理模型（selfie-segmentation .tflite）。
     *
     * 应在 GL 线程或后台线程调用（首次加载 ~100ms）。
     * 模型路径通过 AssetManager 提前 copy 到内部存储：
     *   `context.assets.open("selfie_segmentation.tflite")` → filesDir
     *
     * @param modelPath  .tflite 文件绝对路径
     * @return true = 加载成功；false = TFLite 未集成或文件不存在
     */
    fun loadAIModel(modelPath: String): Boolean = nativeLoadAIModel(modelPath)

    /**
     * 运行时查询 AI 推理引擎是否可用（TFLite 编译期宏 + 模型已加载）。
     */
    fun isAIAvailable(): Boolean = nativeIsAIAvailable()

    /**
     * 设置美颜参数（下次 processFrame() 时生效）。
     *
     * @param filterHandle   BeautyFilter 原生指针（由 NativeBridge 创建并返回）
     * @param smooth         磨皮强度 [0.0, 1.0]，默认 0.6
     * @param whiten         美白强度 [0.0, 1.0]，默认 0.4
     */
    fun setBeautyParams(filterHandle: Long, smooth: Float, whiten: Float) {
        if (filterHandle != 0L) nativeSetBeautyParams(filterHandle, smooth, whiten)
    }

    /**
     * 设置人像分割参数。
     *
     * 需先调用 [loadAIModel] 加载模型才有效果；
     * 未加载时 SegmentationFilter 自动直通原图，不崩溃。
     *
     * @param filterHandle   SegmentationFilter 原生指针
     * @param mode           分割模式，见 [SegmentationMode]
     * @param bgColorArgb    REPLACE_BG 背景色（ARGB 整数，如 0xFF_00_FF_00 = 绿色）
     * @param edgeSoften     边缘软化 [0.0, 1.0]，0 = 硬边，1 = 最软化
     */
    fun setSegmentationParams(
        filterHandle: Long,
        mode: SegmentationMode = SegmentationMode.BLUR_BG,
        bgColorArgb: Int = 0xFF000000.toInt(),
        edgeSoften: Float = 0.5f
    ) {
        if (filterHandle != 0L)
            nativeSetSegmentationParams(filterHandle, mode.value, bgColorArgb, edgeSoften)
    }

    enum class TrackType(val value: Int) {
        MAIN_VIDEO(0), PIP_VIDEO(1), AUDIO_ONLY(2),
        SUBTITLE(3),   // P1-5: caption / text overlay layer
        STICKER(4)     // P1-5: sticker / animated overlay layer
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
        NONE(0),
        CROSSFADE(1),
        WIPE_LEFT(2),
        WIPE_RIGHT(3),
        WIPE_UP(4),
        WIPE_DOWN(5),
        SLIDE_LEFT(6),
        SLIDE_RIGHT(7),
        ZOOM_IN(8),
        FADE_BLACK(9),
        FLASH(10)
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

    /** P1-4: Toggle reverse playback for a clip. */
    fun setClipReversed(zIndex: Int, clipId: String, reversed: Boolean): Int {
        return nativeSetClipReversed(nativeHandle, zIndex, clipId, reversed)
    }

    /**
     * 音调变换（变声）：以半音为单位调整指定片段的音调。
     * @param semitones 正值升调（尖锐），负值降调（低沉），范围 [-24, +24]
     *                  常用值：男→女声 ≈ +6, 女→男声 ≈ -6
     */
    fun setClipPitchShift(zIndex: Int, clipId: String, semitones: Float): Int {
        return nativeSetClipPitchShift(nativeHandle, zIndex, clipId, semitones)
    }

    /**
     * 音频降噪（Wiener Filter）：抑制背景底噪/白噪音/机械噪音。
     * @param strength 降噪强度 [0.0, 1.0]，0=关闭，0.5=适中，1.0=最强（可能损失清晰度）
     */
    fun setClipNoiseReduction(zIndex: Int, clipId: String, strength: Float): Int {
        return nativeSetClipNoiseReduction(nativeHandle, zIndex, clipId, strength)
    }

    /**
     * 音量淡入：从静音线性过渡到原始音量。
     * @param durationUs 淡入时长（微秒），通常 500_000L（0.5 秒）
     */
    fun setClipFadeIn(zIndex: Int, clipId: String, durationUs: Long): Int {
        return nativeSetClipFadeIn(nativeHandle, zIndex, clipId, durationUs)
    }

    /**
     * 音量淡出：从原始音量线性过渡到静音。
     * @param startRelUs  淡出起始相对时间（微秒，相对于 clip 起点）= clipDurationUs - fadeDurationUs
     * @param durationUs  淡出时长（微秒）
     */
    fun setClipFadeOut(zIndex: Int, clipId: String, startRelUs: Long, durationUs: Long): Int {
        return nativeSetClipFadeOut(nativeHandle, zIndex, clipId, startRelUs, durationUs)
    }

    /**
     * PCM 波形峰值提取 — 用于剪辑编辑器波形 UI 渲染。
     *
     * @param pcmData  16-bit 小端 PCM 字节数组（通过 MediaExtractor 解码后传入）
     * @param numBars  目标波形柱数（通常等于 UI 组件像素宽度）
     * @param channels 声道数（1=mono，2=stereo）
     * @return         长度为 [numBars] 的归一化峰值数组 [0.0, 1.0]，失败返回 null
     */
    fun extractWaveformPeaks(pcmData: ByteArray, numBars: Int, channels: Int = 2): FloatArray? {
        return nativeExtractWaveformPeaks(pcmData, numBars, channels)
    }

    fun getNativeHandle(): Long = nativeHandle

    /**
     * P2-5: Save current timeline state as a draft file.
     * @param filePath Absolute path to write the draft (e.g. getCacheDir() + "/draft.svdk")
     * @return 0 on success, negative error code on failure.
     */
    fun saveDraft(filePath: String): Int =
        nativeSaveDraft(nativeHandle, filePath)

    fun release() {
        if (nativeHandle != 0L) {
            nativeReleaseTimeline(nativeHandle)
            nativeHandle = 0L
        }
    }

    companion object {
        /**
         * P2-5: Restore a previously saved draft from disk.
         * @return A new TimelineManager wrapping the loaded timeline,
         *         or null if the file is missing or corrupt.
         */
        fun loadDraft(filePath: String): TimelineManager? {
            System.loadLibrary("video-sdk")
            val handle = nativeLoadDraftStatic(filePath)
            if (handle == 0L) return null
            val mgr = TimelineManager(0, 0, 0)
            mgr.release()
            mgr.nativeHandle = handle
            return mgr
        }

        @JvmStatic private external fun nativeLoadDraftStatic(filePath: String): Long

        /**
         * Static convenience wrapper — safe to call without constructing a full timeline.
         * Uses a minimal (1×1 @1fps) throwaway instance; the JNI function is stateless.
         */
        fun queryIsSoftwareDecoderAvailable(): Boolean {
            return try {
                TimelineManager(1, 1, 1).isSoftwareDecoderAvailable()
            } catch (_: Exception) {
                false
            }
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
    private external fun nativeSetClipReversed(handle: Long, zIndex: Int, clipId: String, reversed: Boolean): Int
    private external fun nativeExtractWaveformPeaks(pcmData: ByteArray, numBars: Int, channels: Int): FloatArray?
    private external fun nativeSetDecoderFallbackStrategy(decoderPoolHandle: Long, strategy: Int)
    private external fun nativeIsSoftwareDecoderAvailable(): Boolean
    private external fun nativeLoadAIModel(modelPath: String): Boolean
    private external fun nativeIsAIAvailable(): Boolean
    private external fun nativeSetBeautyParams(filterHandle: Long, smooth: Float, whiten: Float)
    private external fun nativeSetSegmentationParams(filterHandle: Long, mode: Int, bgColorArgb: Int, edgeSoften: Float)
    private external fun nativeSetClipPitchShift(handle: Long, zIndex: Int, clipId: String, semitones: Float): Int
    private external fun nativeSetClipNoiseReduction(handle: Long, zIndex: Int, clipId: String, strength: Float): Int
    private external fun nativeSetClipFadeIn(handle: Long, zIndex: Int, clipId: String, durationUs: Long): Int
    private external fun nativeSetClipFadeOut(handle: Long, zIndex: Int, clipId: String, startRelUs: Long, durationUs: Long): Int
    private external fun nativeSaveDraft(handle: Long, filePath: String): Int
}
