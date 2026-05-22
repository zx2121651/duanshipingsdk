package com.sdk.video.export

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

/**
 * 导出断点恢复（Export Checkpoint / Resume）。
 *
 * ## 原理
 * 导出过程中每隔 [checkpointIntervalMs] 毫秒（或每完成一个分片）写一个 JSON checkpoint 文件。
 * App 崩溃 / 进程被杀后下次启动时可检测 checkpoint，从上次进度继续导出。
 *
 * ## Checkpoint 文件结构（JSON）
 * ```json
 * {
 *   "jobId": "abc123",
 *   "draftPath": "/data/.../draft.svdk",
 *   "outputPath": "/data/.../out.mp4",
 *   "tempPath": "/data/.../out.mp4.part",
 *   "totalDurationMs": 30000,
 *   "resumePositionMs": 12500,
 *   "chunksCompleted": 5,
 *   "colorSpace": "SDR_BT709",
 *   "bitrate": 8000000,
 *   "fps": 30,
 *   "width": 1080,
 *   "height": 1920,
 *   "timestamp": 1715000000000
 * }
 * ```
 *
 * ## 分片续传策略
 * - 已完成的分片文件保存在 [tempDir]；续传时跳过已完成的分片索引。
 * - 最后一次未完成的分片从头重新编码（避免边界帧损坏）。
 *
 * ## 线程安全
 * 所有磁盘 I/O 在 `Dispatchers.IO` 上执行。
 */
class ExportRecovery(context: Context) {

    data class Checkpoint(
        val jobId: String,
        val draftPath: String,
        val outputPath: String,
        val tempPath: String,
        val totalDurationMs: Long,
        val resumePositionMs: Long,
        val chunksCompleted: Int,
        val colorSpace: String,
        val bitrate: Int,
        val fps: Int,
        val width: Int,
        val height: Int,
        val timestamp: Long = System.currentTimeMillis()
    ) {
        val isComplete: Boolean get() = resumePositionMs >= totalDurationMs

        fun toJson(): String = JSONObject().apply {
            put("jobId",             jobId)
            put("draftPath",         draftPath)
            put("outputPath",        outputPath)
            put("tempPath",          tempPath)
            put("totalDurationMs",   totalDurationMs)
            put("resumePositionMs",  resumePositionMs)
            put("chunksCompleted",   chunksCompleted)
            put("colorSpace",        colorSpace)
            put("bitrate",           bitrate)
            put("fps",               fps)
            put("width",             width)
            put("height",            height)
            put("timestamp",         timestamp)
        }.toString(2)

        companion object {
            fun fromJson(json: String): Checkpoint? = runCatching {
                val o = JSONObject(json)
                Checkpoint(
                    jobId            = o.getString("jobId"),
                    draftPath        = o.getString("draftPath"),
                    outputPath       = o.getString("outputPath"),
                    tempPath         = o.getString("tempPath"),
                    totalDurationMs  = o.getLong("totalDurationMs"),
                    resumePositionMs = o.getLong("resumePositionMs"),
                    chunksCompleted  = o.getInt("chunksCompleted"),
                    colorSpace       = o.getString("colorSpace"),
                    bitrate          = o.getInt("bitrate"),
                    fps              = o.getInt("fps"),
                    width            = o.getInt("width"),
                    height           = o.getInt("height"),
                    timestamp        = o.optLong("timestamp", 0L)
                )
            }.getOrNull()
        }
    }

    companion object {
        private const val TAG = "ExportRecovery"
        private const val CHECKPOINT_DIR = "export_checkpoints"
        private const val TEMP_DIR       = "export_temp"
        const val DEFAULT_INTERVAL_MS    = 5_000L  // checkpoint 间隔
    }

    private val checkpointDir = File(context.cacheDir, CHECKPOINT_DIR).also { it.mkdirs() }
    val tempDir: File          = File(context.cacheDir, TEMP_DIR).also { it.mkdirs() }

    // ── 写 checkpoint ─────────────────────────────────────────────────────────

    suspend fun save(cp: Checkpoint) = withContext(Dispatchers.IO) {
        val file = checkpointFile(cp.jobId)
        try {
            file.writeText(cp.toJson())
            Log.d(TAG, "Checkpoint saved: ${cp.jobId} pos=${cp.resumePositionMs}ms chunks=${cp.chunksCompleted}")
        } catch (e: Exception) {
            Log.e(TAG, "save checkpoint failed: ${cp.jobId}", e)
        }
    }

    // ── 读 checkpoint ─────────────────────────────────────────────────────────

    suspend fun load(jobId: String): Checkpoint? = withContext(Dispatchers.IO) {
        val file = checkpointFile(jobId)
        if (!file.exists()) return@withContext null
        Checkpoint.fromJson(file.readText()).also {
            if (it == null) Log.w(TAG, "Corrupt checkpoint: $jobId")
        }
    }

    /** 检查是否有任何可恢复的导出任务（按时间戳降序）。 */
    suspend fun findResumable(): List<Checkpoint> = withContext(Dispatchers.IO) {
        checkpointDir.listFiles { _, name -> name.endsWith(".json") }
            ?.mapNotNull { Checkpoint.fromJson(it.readText()) }
            ?.filter { !it.isComplete }
            ?.sortedByDescending { it.timestamp }
            ?: emptyList()
    }

    // ── 清理 ──────────────────────────────────────────────────────────────────

    suspend fun remove(jobId: String) = withContext(Dispatchers.IO) {
        checkpointFile(jobId).delete()
        // Remove partial temp files for this job
        tempDir.listFiles { _, name -> name.startsWith(jobId) }
            ?.forEach { it.delete() }
        Log.d(TAG, "Checkpoint removed: $jobId")
    }

    /** 删除所有超过 [maxAgeMs] 毫秒的 checkpoint（定期 GC）。 */
    suspend fun gc(maxAgeMs: Long = 7 * 24 * 3600_000L) = withContext(Dispatchers.IO) {
        val cutoff = System.currentTimeMillis() - maxAgeMs
        checkpointDir.listFiles()?.forEach { f ->
            if (f.lastModified() < cutoff) {
                f.delete()
                Log.d(TAG, "GC removed old checkpoint: ${f.name}")
            }
        }
    }

    // ── 分片管理 ──────────────────────────────────────────────────────────────

    /** 返回某任务已完成的分片文件列表（按分片索引排序）。 */
    fun completedChunks(jobId: String): List<File> =
        tempDir.listFiles { _, name -> name.startsWith("${jobId}_chunk_") }
            ?.sortedBy { extractChunkIndex(it.name) }
            ?: emptyList()

    /** 生成分片临时文件路径。 */
    fun chunkFile(jobId: String, chunkIndex: Int): File =
        File(tempDir, "${jobId}_chunk_${"%04d".format(chunkIndex)}.mp4")

    private fun extractChunkIndex(filename: String): Int =
        filename.substringAfterLast("_chunk_").substringBefore(".").toIntOrNull() ?: -1

    private fun checkpointFile(jobId: String) = File(checkpointDir, "$jobId.json")
}
