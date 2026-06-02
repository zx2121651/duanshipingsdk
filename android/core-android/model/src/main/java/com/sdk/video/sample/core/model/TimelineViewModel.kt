package com.sdk.video.sample.core.model

import android.content.Context
import android.media.MediaMetadataRetriever
import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.sdk.video.timeline.TimelineManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// 视频剪辑常用工具类型枚举
enum class TimelineTool { COLOR, TEXT, AUDIO, EFFECTS, TRANSITION, SPEED }

// 视频导出进度状态枚举
enum class ExportState { IDLE, EXPORTING, DONE, ERROR }

// 单个微调参数状态实体
data class ColorParams(
    val brightness: Float = 0f,
    val contrast:   Float = 0f,
    val saturation: Float = 0f,
    val temperature: Float = 0f,
    val sharpen:    Float = 0f
)

// 字幕文本段落实体
data class TextClipState(
    val id: String,
    val text: String,
    val startUs: Long,
    val endUs: Long,
    val fontSize: Float = 24f,
    val colorArgb: Long = 0xFFFFFFFF
)

// 视频片段实体
data class ClipItem(
    val uri: Uri,
    val durationUs: Long,
    val clipId: String
)

/**
 * 剪辑编辑器核心 ViewModel (TimelineViewModel)
 * 
 * 管理并分发多轨时间线组合状态：
 * - 维护底层 native 的 C++ [TimelineManager] 的实例化与视频轨道装配流程。
 * - 暴露视频片段、字幕段落、播放时钟、关键帧列表及渲染导出状态的数据流给编辑器 UI 组件。
 * - 驱动与协程绑定的播放时钟时序，协调 UI 与底层渲染时间点。
 */
class TimelineViewModel : ViewModel() {

    // ── 导入的视频片段列表 ──────────────────────────────────────────
    private val _clips = MutableStateFlow<List<ClipItem>>(emptyList())
    val clips: StateFlow<List<ClipItem>> = _clips.asStateFlow()

    // ── 底层 C++ SDK 时间轴管理器 ──────────────────────────────────────────
    private val _timelineManager = MutableStateFlow<TimelineManager?>(null)
    val timelineManager: StateFlow<TimelineManager?> = _timelineManager.asStateFlow()

    // ── 当前选中的视频片段索引 ──────────────────────────────────────────
    private val _selectedIndex = MutableStateFlow(0)
    val selectedIndex: StateFlow<Int> = _selectedIndex.asStateFlow()

    // ── 当前编辑器处于激活态的底部工具 Tab ──────────────────────────────
    private val _activeTool = MutableStateFlow<TimelineTool?>(null)
    val activeTool: StateFlow<TimelineTool?> = _activeTool.asStateFlow()

    // ── 微调调色参数 ──────────────────────────────────────────────────
    private val _colorParams = MutableStateFlow(ColorParams())
    val colorParams: StateFlow<ColorParams> = _colorParams.asStateFlow()

    // ── 添加的字幕文本轨片段列表 ──────────────────────────────────────────
    private val _textClips = MutableStateFlow<List<TextClipState>>(emptyList())
    val textClips: StateFlow<List<TextClipState>> = _textClips.asStateFlow()

    // ── 当前片段的播放速率 ────────────────────────────────────────────
    private val _speedFactor = MutableStateFlow(1f)
    val speedFactor: StateFlow<Float> = _speedFactor.asStateFlow()

    // ── 导出进度 (0.0f - 1.0f) ──────────────────────────────────────
    private val _exportProgress = MutableStateFlow(0f)
    val exportProgress: StateFlow<Float> = _exportProgress.asStateFlow()

    // ── 导出运行状态 ──────────────────────────────────────────────────
    private val _exportState = MutableStateFlow(ExportState.IDLE)
    val exportState: StateFlow<ExportState> = _exportState.asStateFlow()

    private val _exportError = MutableStateFlow<String?>(null)
    val exportError: StateFlow<String?> = _exportError.asStateFlow()

    // ── 当前合成视频总时长 (单位：微秒) ───────────────────────────────
    private val _totalDurationUs = MutableStateFlow(0L)
    val totalDurationUs: StateFlow<Long> = _totalDurationUs.asStateFlow()

    // ── 关键帧微秒点列表 ──────────────────────────────────────────────
    private val _keyframes = MutableStateFlow<List<Long>>(emptyList())
    val keyframes: StateFlow<List<Long>> = _keyframes.asStateFlow()

    /** 
     * 播放时钟——与 ViewModel 协程 Scope 绑定生命周期
     * 通过惰性求值 lazy 初始化，确保 player instance 存续
     */
    val player: TimelinePlayer by lazy { TimelinePlayer(viewModelScope, 0L) }

    /**
     * 切换选中时刻的关键帧状态（有则移除，无则新增，并重新依序排列）
     */
    fun toggleKeyframe(posUs: Long) {
        val list = _keyframes.value.toMutableList()
        val threshold = 100_000L // 100毫秒容差值范围
        val existing = list.firstOrNull { Math.abs(it - posUs) < threshold }
        if (existing != null) {
            list.remove(existing)
        } else {
            list.add(posUs)
            list.sort()
        }
        _keyframes.value = list
    }

    fun setActiveTool(tool: TimelineTool?) {
        _activeTool.value = if (_activeTool.value == tool) null else tool
    }

    fun selectClip(index: Int) {
        _selectedIndex.value = index
    }

    /**
     * 异步导入本地视频片段文件
     * 内部利用 MediaMetadataRetriever 在 IO 线程池获取视频精确时长，
     * 并使用 JNI 绑定向底层 C++ TimelineManager 初始化视频音画轨道与片段。
     */
    fun importClips(uris: List<Uri>, context: Context) {
        viewModelScope.launch {
            val items = withContext(Dispatchers.IO) {
                uris.mapIndexed { i, uri ->
                    val dur = getVideoDurationUs(context, uri)
                    ClipItem(uri = uri, durationUs = dur, clipId = "clip_$i")
                }
            }
            _clips.value = items

            // 构造 1080x1920，30帧的标准时间线管理器
            val tm = TimelineManager(1080, 1920, 30)
            tm.addTrack(0, TimelineManager.TrackType.MAIN_VIDEO)

            var timelinePos = 0L
            items.forEach { clip ->
                tm.addClip(
                    zIndex      = 0,
                    clipId      = clip.clipId,
                    sourcePath  = clip.uri.toString(),
                    mediaType   = TimelineManager.MediaType.VIDEO,
                    trimInUs    = 0L,
                    trimOutUs   = clip.durationUs,
                    timelineInUs = timelinePos
                )
                timelinePos += clip.durationUs
            }
            _timelineManager.value = tm

            val totalUs = items.sumOf { it.durationUs }
            _totalDurationUs.value = totalUs
            player.setTotalDuration(totalUs)
        }
    }

    /**
     * 从时间轴移除当前选中的视频片段，并同步底层 JNI 轨道数据
     */
    fun deleteClip() {
        val idx = _selectedIndex.value
        val items = _clips.value.toMutableList()
        if (idx !in items.indices) return
        val removed = items.removeAt(idx)
        _clips.value = items
        _timelineManager.value?.removeClip(0, removed.clipId)
        _selectedIndex.value = idx.coerceAtMost((items.size - 1).coerceAtLeast(0))
    }

    /**
     * 改变当前视频片段的播放速率
     */
    fun setSpeed(speed: Float) {
        _speedFactor.value = speed
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        _timelineManager.value?.setClipSpeed(0, clip.clipId, speed)
    }

    /**
     * 更新滤镜调色微调值
     */
    fun setColorParam(key: String, value: Float) {
        _colorParams.value = when (key) {
            "brightness"  -> _colorParams.value.copy(brightness  = value)
            "contrast"    -> _colorParams.value.copy(contrast    = value)
            "saturation"  -> _colorParams.value.copy(saturation  = value)
            "temperature" -> _colorParams.value.copy(temperature = value)
            "sharpen"     -> _colorParams.value.copy(sharpen     = value)
            else -> _colorParams.value
        }
    }

    /**
     * 在指定区间插入一段渲染字幕文本
     */
    fun addTextClip(text: String, startUs: Long, endUs: Long, fontSize: Float, colorArgb: Long) {
        val id = "text_${System.currentTimeMillis()}"
        _textClips.value = _textClips.value + TextClipState(id, text, startUs, endUs, fontSize, colorArgb)
    }

    fun removeTextClip(id: String) {
        _textClips.value = _textClips.value.filter { it.id != id }
    }

    /**
     * 为当前选中的视频轨道边界配置转场特效
     */
    fun setTransition(type: TimelineManager.TransitionType, durationUs: Long) {
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        _timelineManager.value?.setClipTransition(0, clip.clipId, type, durationUs)
    }

    /**
     * 设定音频淡入时长
     */
    fun setAudioFadeIn(durationUs: Long) {
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        _timelineManager.value?.setClipFadeIn(0, clip.clipId, durationUs)
    }

    /**
     * 设定音频淡出时刻与淡出时长
     */
    fun setAudioFadeOut(durationUs: Long) {
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        val fadeDur = durationUs
        val startRel = (clip.durationUs - fadeDur).coerceAtLeast(0L)
        _timelineManager.value?.setClipFadeOut(0, clip.clipId, startRel, fadeDur)
    }

    /**
     * 设置音频降噪参数
     */
    fun setNoiseReduction(strength: Float) {
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        _timelineManager.value?.setClipNoiseReduction(0, clip.clipId, strength)
    }

    /**
     * 异步启动视频合成与文件写盘导出流程
     */
    fun startExport(outputPath: String, width: Int, height: Int, fps: Int, bitrate: Int) {
        _exportState.value = ExportState.EXPORTING
        _exportProgress.value = 0f
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                // 模拟多帧合成压缩延时
                for (i in 1..20) {
                    kotlinx.coroutines.delay(100)
                    _exportProgress.value = i / 20f
                }
            }
            _exportState.value = ExportState.DONE
        }
    }

    fun resetExport() {
        _exportState.value = ExportState.IDLE
        _exportProgress.value = 0f
        _exportError.value = null
    }

    override fun onCleared() {
        super.onCleared()
        player.release()
        _timelineManager.value?.release()
    }

    private fun getVideoDurationUs(context: Context, uri: Uri): Long {
        return try {
            val mmr = MediaMetadataRetriever()
            mmr.setDataSource(context, uri)
            val durMs = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 5000L
            mmr.release()
            durMs * 1000L
        } catch (_: Exception) {
            5_000_000L
        }
    }
}
