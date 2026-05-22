package com.sdk.video.sample.state

import android.content.Context
import android.media.MediaMetadataRetriever
import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.sdk.video.timeline.TimelineManager
import com.sdk.video.sample.ui.TimelinePlayer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class TimelineTool { COLOR, TEXT, AUDIO, EFFECTS, TRANSITION, SPEED }

enum class ExportState { IDLE, EXPORTING, DONE, ERROR }

data class ColorParams(
    val brightness: Float = 0f,
    val contrast:   Float = 0f,
    val saturation: Float = 0f,
    val temperature: Float = 0f,
    val sharpen:    Float = 0f
)

data class TextClipState(
    val id: String,
    val text: String,
    val startUs: Long,
    val endUs: Long,
    val fontSize: Float = 24f,
    val colorArgb: Long = 0xFFFFFFFF
)

data class ClipItem(
    val uri: Uri,
    val durationUs: Long,
    val clipId: String
)

class TimelineViewModel : ViewModel() {

    private val _clips = MutableStateFlow<List<ClipItem>>(emptyList())
    val clips: StateFlow<List<ClipItem>> = _clips.asStateFlow()

    private val _timelineManager = MutableStateFlow<TimelineManager?>(null)
    val timelineManager: StateFlow<TimelineManager?> = _timelineManager.asStateFlow()

    private val _selectedIndex = MutableStateFlow(0)
    val selectedIndex: StateFlow<Int> = _selectedIndex.asStateFlow()

    private val _activeTool = MutableStateFlow<TimelineTool?>(null)
    val activeTool: StateFlow<TimelineTool?> = _activeTool.asStateFlow()

    private val _colorParams = MutableStateFlow(ColorParams())
    val colorParams: StateFlow<ColorParams> = _colorParams.asStateFlow()

    private val _textClips = MutableStateFlow<List<TextClipState>>(emptyList())
    val textClips: StateFlow<List<TextClipState>> = _textClips.asStateFlow()

    private val _speedFactor = MutableStateFlow(1f)
    val speedFactor: StateFlow<Float> = _speedFactor.asStateFlow()

    private val _exportProgress = MutableStateFlow(0f)
    val exportProgress: StateFlow<Float> = _exportProgress.asStateFlow()

    private val _exportState = MutableStateFlow(ExportState.IDLE)
    val exportState: StateFlow<ExportState> = _exportState.asStateFlow()

    private val _exportError = MutableStateFlow<String?>(null)
    val exportError: StateFlow<String?> = _exportError.asStateFlow()

    private val _totalDurationUs = MutableStateFlow(0L)
    val totalDurationUs: StateFlow<Long> = _totalDurationUs.asStateFlow()

    private val _keyframes = MutableStateFlow<List<Long>>(emptyList())
    val keyframes: StateFlow<List<Long>> = _keyframes.asStateFlow()

    /** Playback clock — drives currentPositionUs at 30 fps during preview. */
    val player: TimelinePlayer by lazy { TimelinePlayer(viewModelScope, 0L) }

    fun toggleKeyframe(posUs: Long) {
        val list = _keyframes.value.toMutableList()
        val threshold = 100_000L // 100ms tolerance
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

    fun importClips(uris: List<Uri>, context: Context) {
        viewModelScope.launch {
            val items = withContext(Dispatchers.IO) {
                uris.mapIndexed { i, uri ->
                    val dur = getVideoDurationUs(context, uri)
                    ClipItem(uri = uri, durationUs = dur, clipId = "clip_$i")
                }
            }
            _clips.value = items

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

    fun deleteClip() {
        val idx = _selectedIndex.value
        val items = _clips.value.toMutableList()
        if (idx !in items.indices) return
        val removed = items.removeAt(idx)
        _clips.value = items
        _timelineManager.value?.removeClip(0, removed.clipId)
        _selectedIndex.value = idx.coerceAtMost((items.size - 1).coerceAtLeast(0))
    }

    fun setSpeed(speed: Float) {
        _speedFactor.value = speed
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        _timelineManager.value?.setClipSpeed(0, clip.clipId, speed)
    }

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

    fun addTextClip(text: String, startUs: Long, endUs: Long, fontSize: Float, colorArgb: Long) {
        val id = "text_${System.currentTimeMillis()}"
        _textClips.value = _textClips.value + TextClipState(id, text, startUs, endUs, fontSize, colorArgb)
    }

    fun removeTextClip(id: String) {
        _textClips.value = _textClips.value.filter { it.id != id }
    }

    fun setTransition(type: TimelineManager.TransitionType, durationUs: Long) {
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        _timelineManager.value?.setClipTransition(0, clip.clipId, type, durationUs)
    }

    fun setAudioFadeIn(durationUs: Long) {
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        _timelineManager.value?.setClipFadeIn(0, clip.clipId, durationUs)
    }

    fun setAudioFadeOut(durationUs: Long) {
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        val fadeDur = durationUs
        val startRel = (clip.durationUs - fadeDur).coerceAtLeast(0L)
        _timelineManager.value?.setClipFadeOut(0, clip.clipId, startRel, fadeDur)
    }

    fun setNoiseReduction(strength: Float) {
        val idx = _selectedIndex.value
        val clip = _clips.value.getOrNull(idx) ?: return
        _timelineManager.value?.setClipNoiseReduction(0, clip.clipId, strength)
    }

    fun startExport(outputPath: String, width: Int, height: Int, fps: Int, bitrate: Int) {
        _exportState.value = ExportState.EXPORTING
        _exportProgress.value = 0f
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
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
