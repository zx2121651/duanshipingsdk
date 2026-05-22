package com.sdk.video.timeline

import android.graphics.Color
import androidx.lifecycle.ViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

// ---------------------------------------------------------------------------
// UI data models (mirror C++ SubtitleClip / StickerClip)
// ---------------------------------------------------------------------------

data class SubtitleEntry(
    val id: String,
    val text: String,
    val startMs: Long,
    val endMs: Long,
    val x: Float = 0.5f,
    val y: Float = 0.85f,
    val fontSizePx: Float = 48f,
    val textColor: Int = Color.WHITE,
    val bgColor: Int = 0x80000000.toInt(),
    val alignment: Int = 1
) {
    val durationMs: Long get() = endMs - startMs
}

data class StickerEntry(
    val id: String,
    val imagePath: String,
    val startMs: Long,
    val endMs: Long,
    val centerX: Float = 0.5f,
    val centerY: Float = 0.5f,
    val scale: Float = 0.3f,
    val rotation: Float = 0f
) {
    val durationMs: Long get() = endMs - startMs
}

enum class EasingType(val intValue: Int) {
    LINEAR(0), EASE_IN(1), EASE_OUT(2), EASE_IN_OUT(3), HOLD(4)
}

data class KeyframeUiEntry(
    val clipId: String,
    val paramName: String,
    val timeMs: Long,
    val value: Float,
    val easing: EasingType = EasingType.LINEAR
)

sealed class TimelineSelection {
    object None : TimelineSelection()
    data class SubtitleSelected(val id: String) : TimelineSelection()
    data class StickerSelected(val id: String)  : TimelineSelection()
}

// ---------------------------------------------------------------------------
// ViewModel
// ---------------------------------------------------------------------------
class TimelineEditorViewModel : ViewModel() {

    private val _subtitles = MutableStateFlow<List<SubtitleEntry>>(emptyList())
    val subtitles: StateFlow<List<SubtitleEntry>> = _subtitles.asStateFlow()

    private val _stickers = MutableStateFlow<List<StickerEntry>>(emptyList())
    val stickers: StateFlow<List<StickerEntry>> = _stickers.asStateFlow()

    private val _keyframes = MutableStateFlow<List<KeyframeUiEntry>>(emptyList())
    val keyframes: StateFlow<List<KeyframeUiEntry>> = _keyframes.asStateFlow()

    private val _selection = MutableStateFlow<TimelineSelection>(TimelineSelection.None)
    val selection: StateFlow<TimelineSelection> = _selection.asStateFlow()

    private val _playheadMs = MutableStateFlow(0L)
    val playheadMs: StateFlow<Long> = _playheadMs.asStateFlow()

    // ── 字幕操作 ────────────────────────────────────────────────────────────

    fun addSubtitle(
        text: String,
        startMs: Long,
        durationMs: Long = 3_000L,
        y: Float = 0.85f,
        fontSizePx: Float = 48f
    ): String {
        val id = "sub_${System.nanoTime()}"
        _subtitles.update { it + SubtitleEntry(
            id = id, text = text,
            startMs = startMs, endMs = startMs + durationMs,
            y = y, fontSizePx = fontSizePx
        )}
        _selection.value = TimelineSelection.SubtitleSelected(id)
        return id
    }

    fun updateSubtitle(id: String, block: SubtitleEntry.() -> SubtitleEntry) {
        _subtitles.update { list -> list.map { if (it.id == id) it.block() else it } }
    }

    fun removeSubtitle(id: String) {
        _subtitles.update { it.filter { s -> s.id != id } }
        _keyframes.update { it.filter { kf -> kf.clipId != id } }
        if ((_selection.value as? TimelineSelection.SubtitleSelected)?.id == id)
            _selection.value = TimelineSelection.None
    }

    // ── 贴纸操作 ────────────────────────────────────────────────────────────

    fun addSticker(
        imagePath: String,
        startMs: Long,
        durationMs: Long = 5_000L,
        centerX: Float = 0.5f,
        centerY: Float = 0.5f
    ): String {
        val id = "sticker_${System.nanoTime()}"
        _stickers.update { it + StickerEntry(
            id = id, imagePath = imagePath,
            startMs = startMs, endMs = startMs + durationMs,
            centerX = centerX, centerY = centerY
        )}
        _selection.value = TimelineSelection.StickerSelected(id)
        return id
    }

    fun updateSticker(id: String, block: StickerEntry.() -> StickerEntry) {
        _stickers.update { list -> list.map { if (it.id == id) it.block() else it } }
    }

    fun removeSticker(id: String) {
        _stickers.update { it.filter { s -> s.id != id } }
        _keyframes.update { it.filter { kf -> kf.clipId != id } }
        if ((_selection.value as? TimelineSelection.StickerSelected)?.id == id)
            _selection.value = TimelineSelection.None
    }

    // ── 关键帧操作 ──────────────────────────────────────────────────────────

    fun addKeyframe(
        clipId: String,
        paramName: String,
        timeMs: Long,
        value: Float,
        easing: EasingType = EasingType.LINEAR
    ) {
        _keyframes.update { list ->
            val existing = list.indexOfFirst { it.clipId == clipId && it.paramName == paramName && it.timeMs == timeMs }
            if (existing >= 0) {
                list.toMutableList().also { it[existing] = it[existing].copy(value = value, easing = easing) }
            } else {
                list + KeyframeUiEntry(clipId, paramName, timeMs, value, easing)
            }
        }
    }

    fun removeKeyframe(clipId: String, paramName: String, timeMs: Long) {
        _keyframes.update { it.filter { kf ->
            !(kf.clipId == clipId && kf.paramName == paramName && kf.timeMs == timeMs)
        }}
    }

    fun getKeyframesForClip(clipId: String): List<KeyframeUiEntry> =
        _keyframes.value.filter { it.clipId == clipId }

    // ── 播放头 / 选区 ───────────────────────────────────────────────────────

    fun seekTo(ms: Long) { _playheadMs.value = ms.coerceAtLeast(0L) }
    fun select(sel: TimelineSelection) { _selection.value = sel }
    fun clearSelection() { _selection.value = TimelineSelection.None }

    // ── 查询 ────────────────────────────────────────────────────────────────

    /** 返回当前播放头可见的字幕（时间范围内）。 */
    fun activeSubtitlesAt(ms: Long): List<SubtitleEntry> =
        _subtitles.value.filter { ms in it.startMs until it.endMs }

    /** 返回当前播放头可见的贴纸。 */
    fun activeStickersAt(ms: Long): List<StickerEntry> =
        _stickers.value.filter { ms in it.startMs until it.endMs }

    /** 按开始时间排序的所有字幕（用于字幕轨道 UI 渲染）。 */
    val sortedSubtitles: List<SubtitleEntry>
        get() = _subtitles.value.sortedBy { it.startMs }

    /** 按开始时间排序的所有贴纸。 */
    val sortedStickers: List<StickerEntry>
        get() = _stickers.value.sortedBy { it.startMs }

    // ── 草稿往返 (UI → 字符串，用于传递给 C++ TimelineDraft) ───────────────

    /**
     * 将字幕/贴纸列表导出为 TimelineDraft SUB: / STICKER: 行的字符串片段。
     * 调用方将此字符串与 C++ serializeTimeline() 输出拼接后写入草稿文件。
     */
    fun exportDraftFragment(): String {
        val sb = StringBuilder()
        for (s in _subtitles.value) {
            sb.append("SUB:")
                .append(s.id.escapeDraft()).append(":")
                .append(s.text.escapeDraft()).append(":")
                .append(s.startMs * 1_000_000L).append(":")
                .append(s.durationMs * 1_000_000L).append(":")
                .append(s.x).append(":")
                .append(s.y).append(":")
                .append(s.fontSizePx).append(":")
                .append(s.textColor.toUInt()).append(":")
                .append(s.bgColor.toUInt()).append(":")
                .append(s.alignment).append("\n")
        }
        for (st in _stickers.value) {
            sb.append("STICKER:")
                .append(st.id.escapeDraft()).append(":")
                .append(st.imagePath.escapeDraft()).append(":")
                .append(st.durationMs * 1_000_000L).append(":")
                .append(st.startMs * 1_000_000L).append(":")
                .append(st.centerX).append(":")
                .append(st.centerY).append(":")
                .append(st.scale).append(":")
                .append(st.rotation).append("\n")
        }
        return sb.toString()
    }

    /**
     * 从 TimelineDraft 草稿行字符串片段恢复字幕/贴纸列表。
     * 格式同 C++ TimelineDraft.h SUB:/STICKER: 行。
     */
    fun importDraftFragment(draftText: String) {
        val subs = mutableListOf<SubtitleEntry>()
        val stks = mutableListOf<StickerEntry>()
        for (line in draftText.lineSequence()) {
            val parts = line.split(":")
            when {
                parts.size >= 11 && parts[0] == "SUB" -> {
                    val tlInNs  = parts[3].toLongOrNull() ?: continue
                    val durNs   = parts[4].toLongOrNull() ?: continue
                    subs += SubtitleEntry(
                        id         = parts[1].unescapeDraft(),
                        text       = parts[2].unescapeDraft(),
                        startMs    = tlInNs / 1_000_000L,
                        endMs      = (tlInNs + durNs) / 1_000_000L,
                        x          = parts[5].toFloatOrNull() ?: 0.5f,
                        y          = parts[6].toFloatOrNull() ?: 0.85f,
                        fontSizePx = parts[7].toFloatOrNull() ?: 48f,
                        textColor  = parts[8].toUIntOrNull()?.toInt() ?: Color.WHITE,
                        bgColor    = parts[9].toUIntOrNull()?.toInt() ?: 0x80000000.toInt(),
                        alignment  = parts[10].toIntOrNull() ?: 1
                    )
                }
                parts.size >= 9 && parts[0] == "STICKER" -> {
                    val durNs  = parts[3].toLongOrNull() ?: continue
                    val tlInNs = parts[4].toLongOrNull() ?: continue
                    stks += StickerEntry(
                        id        = parts[1].unescapeDraft(),
                        imagePath = parts[2].unescapeDraft(),
                        startMs   = tlInNs / 1_000_000L,
                        endMs     = (tlInNs + durNs) / 1_000_000L,
                        centerX   = parts[5].toFloatOrNull() ?: 0.5f,
                        centerY   = parts[6].toFloatOrNull() ?: 0.5f,
                        scale     = parts[7].toFloatOrNull() ?: 0.3f,
                        rotation  = parts[8].toFloatOrNull() ?: 0f
                    )
                }
            }
        }
        _subtitles.value = subs
        _stickers.value  = stks
    }

    private fun String.escapeDraft() = replace("\\", "\\\\").replace(":", "\\c").replace("\n", "\\n")
    private fun String.unescapeDraft(): String {
        val sb = StringBuilder()
        var i = 0
        while (i < length) {
            if (this[i] == '\\' && i + 1 < length) {
                when (this[i + 1]) {
                    'c'  -> { sb.append(':');  i += 2 }
                    '\\' -> { sb.append('\\'); i += 2 }
                    'n'  -> { sb.append('\n'); i += 2 }
                    else -> { sb.append(this[i]); i++ }
                }
            } else { sb.append(this[i]); i++ }
        }
        return sb.toString()
    }
}
