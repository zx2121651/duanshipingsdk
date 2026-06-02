package com.sdk.video.timeline

import android.content.Context
import android.util.Log
import kotlinx.coroutines.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * TemplateController
 *
 * 模板一键成片引擎（Kotlin 端实现，镜像 C++ TemplateEngine）。
 *
 * 职责：
 *   1. 从 JSON 文件或字符串解析 [VideoTemplate]
 *   2. 将用户素材路径填入模板 slot
 *   3. 生成可提交给 [TimelineManager] 的 [TimelineSpec]，供后续导出
 *
 * 模板 JSON 格式（简化示例）：
 * {
 *   "id": "romantic_v1",
 *   "name": "浪漫回忆",
 *   "aspectRatio": "9:16",
 *   "music": { "type": "asset", "path": "bgm/romantic.mp3", "loopable": true },
 *   "slots": [
 *     { "id": "s0", "durationMs": 3000, "transition": "FADE", "hint": "close-up" },
 *     { "id": "s1", "durationMs": 2500, "transition": "WIPE_LEFT", "hint": "landscape" }
 *   ],
 *   "subtitles": [
 *     { "id": "sub0", "text": "与你相遇的瞬间", "startMs": 0, "endMs": 2000 }
 *   ]
 * }
 *
 * 用法：
 *   val ctrl = TemplateController(context)
 *   ctrl.loadFromAssets("templates/romantic.json")?.let { tmpl ->
 *       tmpl.slots[0].filledPath = "/sdcard/DCIM/clip1.mp4"
 *       tmpl.slots[1].filledPath = "/sdcard/DCIM/photo2.jpg"
 *       val spec = ctrl.buildTimelineSpec(tmpl)
 *       timelineManager.applySpec(spec)
 *   }
 */
class TemplateController(private val context: Context) {

    // ---------------------------------------------------------------------------
    // Data classes
    // ---------------------------------------------------------------------------

    data class TemplateSlot(
        val id: String,
        val durationMs: Long,
        val transition: String = "NONE",
        val hint: String = "",
        var filledPath: String = ""
    ) {
        val isFilled: Boolean get() = filledPath.isNotBlank()
    }

    data class TemplateAudio(
        val type: String = "none",    // "none" | "asset" | "original"
        val path: String = "",
        val loopable: Boolean = false,
        val volumeScale: Float = 1f
    )

    data class TemplateSubtitle(
        val id: String,
        val text: String,
        val startMs: Long,
        val endMs: Long
    )

    data class VideoTemplate(
        val id: String,
        val name: String,
        val aspectRatio: String = "9:16",
        val slots: MutableList<TemplateSlot> = mutableListOf(),
        val audio: TemplateAudio = TemplateAudio(),
        val subtitles: List<TemplateSubtitle> = emptyList()
    ) {
        val totalDurationMs: Long get() = slots.sumOf { it.durationMs }
        val allSlotsFilled: Boolean get() = slots.isNotEmpty() && slots.all { it.isFilled }
        val requiredSlotCount: Int get() = slots.size
    }

    /** 提交给 TimelineManager 的轻量规格描述（平台无关）。 */
    data class TimelineSpec(
        val clips: List<ClipSpec>,
        val audioPath: String?,
        val audioLoop: Boolean,
        val audioVolumeScale: Float,
        val subtitles: List<TemplateSubtitle>,
        val aspectRatio: String
    )

    data class ClipSpec(
        val slotId: String,
        val mediaPath: String,
        val durationMs: Long,
        val transition: String
    )

    // ---------------------------------------------------------------------------
    // Load
    // ---------------------------------------------------------------------------

    /** 从 assets 目录加载模板 JSON。 */
    fun loadFromAssets(assetPath: String): VideoTemplate? {
        return try {
            val json = context.assets.open(assetPath).bufferedReader().readText()
            parseJson(json)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load template from assets: $assetPath", e)
            null
        }
    }

    /** 从文件系统路径加载模板 JSON。 */
    fun loadFromFile(filePath: String): VideoTemplate? {
        return try {
            val json = File(filePath).readText()
            parseJson(json)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load template from file: $filePath", e)
            null
        }
    }

    /** 从 JSON 字符串解析模板。 */
    fun parseJson(json: String): VideoTemplate? {
        return try {
            val obj = JSONObject(json)
            val slots = mutableListOf<TemplateSlot>()
            val slotsArr = obj.optJSONArray("slots") ?: JSONArray()
            for (i in 0 until slotsArr.length()) {
                val s = slotsArr.getJSONObject(i)
                slots += TemplateSlot(
                    id          = s.optString("id", "s$i"),
                    durationMs  = s.optLong("durationMs", 3000L),
                    transition  = s.optString("transition", "NONE"),
                    hint        = s.optString("hint", "")
                )
            }
            val audioObj = obj.optJSONObject("music") ?: obj.optJSONObject("audio")
            val audio = if (audioObj != null) TemplateAudio(
                type        = audioObj.optString("type", "none"),
                path        = audioObj.optString("path", ""),
                loopable    = audioObj.optBoolean("loopable", false),
                volumeScale = audioObj.optDouble("volumeScale", 1.0).toFloat()
            ) else TemplateAudio()

            val subsArr = obj.optJSONArray("subtitles") ?: JSONArray()
            val subtitles = (0 until subsArr.length()).map { i ->
                val s = subsArr.getJSONObject(i)
                TemplateSubtitle(
                    id      = s.optString("id", "sub$i"),
                    text    = s.optString("text", ""),
                    startMs = s.optLong("startMs", 0L),
                    endMs   = s.optLong("endMs", 2000L)
                )
            }
            VideoTemplate(
                id          = obj.optString("id", "template_${System.currentTimeMillis()}"),
                name        = obj.optString("name", "Untitled Template"),
                aspectRatio = obj.optString("aspectRatio", "9:16"),
                slots       = slots,
                audio       = audio,
                subtitles   = subtitles
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse template JSON", e)
            null
        }
    }

    /** 扫描目录，批量加载所有 .json 模板。 */
    fun loadAllFromDirectory(dirPath: String): List<VideoTemplate> {
        val dir = File(dirPath)
        if (!dir.isDirectory) return emptyList()
        return dir.listFiles { f -> f.extension == "json" }
            ?.mapNotNull { loadFromFile(it.absolutePath) }
            ?: emptyList()
    }

    // ---------------------------------------------------------------------------
    // Apply — convert filled template to TimelineSpec
    // ---------------------------------------------------------------------------

    /**
     * 将已填充素材的模板转换为 [TimelineSpec]。
     * @throws IllegalStateException 若有 slot 未填充
     */
    fun buildTimelineSpec(template: VideoTemplate): TimelineSpec {
        check(template.allSlotsFilled) {
            "Template '${template.name}' has unfilled slots: " +
                template.slots.filter { !it.isFilled }.map { it.id }
        }
        val clips = template.slots.map { slot ->
            ClipSpec(
                slotId     = slot.id,
                mediaPath  = slot.filledPath,
                durationMs = slot.durationMs,
                transition = slot.transition
            )
        }
        return TimelineSpec(
            clips            = clips,
            audioPath        = template.audio.path.takeIf { it.isNotBlank() },
            audioLoop        = template.audio.loopable,
            audioVolumeScale = template.audio.volumeScale,
            subtitles        = template.subtitles,
            aspectRatio      = template.aspectRatio
        )
    }

    /**
     * 便捷：自动填充素材路径（按 slot 顺序），然后生成 [TimelineSpec]。
     * @param template      模板（slot 数量须与 mediaPaths 匹配）
     * @param mediaPaths    素材绝对路径列表，按顺序对应每个 slot
     */
    fun buildTimelineSpec(
        template: VideoTemplate,
        mediaPaths: List<String>
    ): TimelineSpec {
        require(mediaPaths.size >= template.slots.size) {
            "Need ${template.slots.size} media files, got ${mediaPaths.size}"
        }
        mediaPaths.forEachIndexed { i, path ->
            if (i < template.slots.size) template.slots[i].filledPath = path
        }
        return buildTimelineSpec(template)
    }

    // ---------------------------------------------------------------------------
    // Built-in example templates (for demos and testing)
    // ---------------------------------------------------------------------------

    /** 生成内置示例模板（无需读取文件）。 */
    fun builtInTemplates(): List<VideoTemplate> = listOf(
        VideoTemplate(
            id    = "builtin_romantic",
            name  = "浪漫回忆",
            slots = mutableListOf(
                TemplateSlot("s0", 3000, "FADE",      "close-up portrait"),
                TemplateSlot("s1", 2500, "WIPE_LEFT", "scenic view"),
                TemplateSlot("s2", 3000, "DISSOLVE",  "smiling face"),
                TemplateSlot("s3", 2000, "FADE",      "hands together")
            ),
            audio = TemplateAudio("asset", "bgm/romantic.mp3", loopable = true),
            subtitles = listOf(
                TemplateSubtitle("sub0", "与你相遇的瞬间", 0L, 2500L),
                TemplateSubtitle("sub1", "是最美好的记忆", 3000L, 5500L)
            )
        ),
        VideoTemplate(
            id    = "builtin_travel",
            name  = "旅行日记",
            slots = mutableListOf(
                TemplateSlot("s0", 2000, "WIPE_LEFT", "destination sign"),
                TemplateSlot("s1", 3000, "NONE",      "landscape"),
                TemplateSlot("s2", 2000, "ZOOM_IN",   "local food"),
                TemplateSlot("s3", 2500, "FADE",      "selfie"),
                TemplateSlot("s4", 2000, "WIPE_LEFT", "sunset")
            ),
            audio = TemplateAudio("asset", "bgm/travel.mp3", loopable = true)
        ),
        VideoTemplate(
            id    = "builtin_product",
            name  = "产品展示",
            slots = mutableListOf(
                TemplateSlot("s0", 2000, "FADE",      "product overview"),
                TemplateSlot("s1", 3000, "NONE",      "detail close-up"),
                TemplateSlot("s2", 2500, "DISSOLVE",  "in-use scenario")
            ),
            audio = TemplateAudio("asset", "bgm/upbeat.mp3", loopable = false)
        )
    )

    private companion object {
        private const val TAG = "TemplateController"
    }
}
