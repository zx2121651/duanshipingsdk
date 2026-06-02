package com.sdk.video.ai

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL

/**
 * 模型热更新客户端（OTA Model Update）。
 *
 * ## 更新流程
 * ```
 * 1. fetchServerManifest(url)   → List<RemoteModelEntry>
 * 2. resolveUpdates(entries)    → 过滤出本地版本 < 远端版本的条目
 * 3. download(entry, progress)  → 下载到 .tmp 文件
 * 4. verifySha256(tmpFile)      → 校验完整性
 * 5. atomicInstall(tmpFile)     → rename → .tflite，更新 ModelVersionRegistry
 * ```
 *
 * ## 服务端 Manifest 格式（JSON）
 * ```json
 * {
 *   "sdkVersion": "1.0",
 *   "models": [
 *     {
 *       "name":      "face_landmark.tflite",
 *       "version":   "20240601",
 *       "url":       "https://cdn.example.com/models/face_landmark_v20240601.tflite",
 *       "sha256":    "a3f...",
 *       "sizeBytes": 4194304
 *     }
 *   ]
 * }
 * ```
 *
 * ## 线程安全
 * 所有网络/磁盘 I/O 在 `Dispatchers.IO` 上执行。
 * 安装过程通过文件原子 rename 保证不会写出半截文件。
 */
class ModelUpdateClient(
    context: Context,
    private val registry: ModelVersionRegistry
) {
    data class RemoteModelEntry(
        val name: String,
        val version: String,
        val url: String,
        val sha256: String,
        val sizeBytes: Long
    )

    sealed class UpdateResult {
        data class Success(val name: String, val path: String, val version: String) : UpdateResult()
        data class UpToDate(val name: String) : UpdateResult()
        data class Failure(val name: String, val reason: String) : UpdateResult()
    }

    companion object {
        private const val TAG = "ModelUpdateClient"
        private const val CONNECT_TIMEOUT_MS = 10_000
        private const val READ_TIMEOUT_MS    = 60_000
    }

    private val modelsDir = File(context.filesDir, "models").also { it.mkdirs() }

    // ── 公开 API ──────────────────────────────────────────────────────────────

    /**
     * 从服务端拉取最新 manifest，并返回所有可更新条目的更新结果。
     *
     * @param manifestUrl  服务端 JSON manifest 的 HTTPS URL
     * @param onProgress   (modelName, bytesDownloaded, totalBytes) 下载进度回调
     */
    suspend fun syncModels(
        manifestUrl: String,
        onProgress: ((name: String, downloaded: Long, total: Long) -> Unit)? = null
    ): List<UpdateResult> = withContext(Dispatchers.IO) {
        val entries = try {
            fetchServerManifest(manifestUrl)
        } catch (e: Exception) {
            Log.e(TAG, "fetchServerManifest failed: $manifestUrl", e)
            return@withContext listOf()
        }

        entries.map { entry ->
            if (registry.isUpToDate(entry.name, entry.version)) {
                Log.d(TAG, "${entry.name} already at ${entry.version}")
                UpdateResult.UpToDate(entry.name)
            } else {
                downloadAndInstall(entry) { dl, total -> onProgress?.invoke(entry.name, dl, total) }
            }
        }
    }

    /**
     * 直接强制更新单个模型（用于 A/B 测试或紧急修复）。
     */
    suspend fun forceUpdate(entry: RemoteModelEntry): UpdateResult = withContext(Dispatchers.IO) {
        downloadAndInstall(entry, onProgress = null)
    }

    // ── 私有实现 ──────────────────────────────────────────────────────────────

    private fun fetchServerManifest(url: String): List<RemoteModelEntry> {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = CONNECT_TIMEOUT_MS
            readTimeout    = READ_TIMEOUT_MS
            requestMethod  = "GET"
            setRequestProperty("Accept", "application/json")
        }
        try {
            if (conn.responseCode != HttpURLConnection.HTTP_OK)
                throw RuntimeException("HTTP ${conn.responseCode} from $url")
            val json = conn.inputStream.bufferedReader().readText()
            return parseManifest(json)
        } finally {
            conn.disconnect()
        }
    }

    private fun parseManifest(json: String): List<RemoteModelEntry> {
        val root    = JSONObject(json)
        val arr     = root.getJSONArray("models")
        val entries = mutableListOf<RemoteModelEntry>()
        for (i in 0 until arr.length()) {
            val obj = arr.getJSONObject(i)
            entries += RemoteModelEntry(
                name      = obj.getString("name"),
                version   = obj.getString("version"),
                url       = obj.getString("url"),
                sha256    = obj.optString("sha256", ""),
                sizeBytes = obj.optLong("sizeBytes", 0L)
            )
        }
        return entries
    }

    private fun downloadAndInstall(
        entry: RemoteModelEntry,
        onProgress: ((downloaded: Long, total: Long) -> Unit)?
    ): UpdateResult {
        val tmpFile  = File(modelsDir, "${entry.name}.tmp")
        val destFile = File(modelsDir, entry.name)
        return try {
            // ── Download to .tmp ──────────────────────────────────────────────
            download(entry.url, tmpFile, entry.sizeBytes, onProgress)

            // ── Verify checksum ───────────────────────────────────────────────
            if (entry.sha256.isNotEmpty()) {
                val actual = registry.computeSha256(tmpFile)
                if (actual != entry.sha256) {
                    tmpFile.delete()
                    return UpdateResult.Failure(entry.name,
                        "SHA-256 mismatch: expected=${entry.sha256} actual=$actual")
                }
            }

            // ── Atomic install: rename .tmp → .tflite ─────────────────────────
            if (destFile.exists()) destFile.delete()
            if (!tmpFile.renameTo(destFile)) {
                // Fallback: copy + delete on rename failure (cross-filesystem)
                tmpFile.copyTo(destFile, overwrite = true)
                tmpFile.delete()
            }

            // ── Update registry ───────────────────────────────────────────────
            registry.put(entry.name, ModelVersionRegistry.ModelInfo(
                version   = entry.version,
                sha256    = entry.sha256,
                sizeBytes = destFile.length(),
                source    = "ota"
            ))

            Log.i(TAG, "Installed ${entry.name} v${entry.version} → ${destFile.absolutePath}")
            UpdateResult.Success(entry.name, destFile.absolutePath, entry.version)

        } catch (e: Exception) {
            tmpFile.delete()
            Log.e(TAG, "downloadAndInstall failed: ${entry.name}", e)
            UpdateResult.Failure(entry.name, e.message ?: "unknown error")
        }
    }

    private fun download(
        url: String,
        dest: File,
        expectedBytes: Long,
        onProgress: ((downloaded: Long, total: Long) -> Unit)?
    ) {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = CONNECT_TIMEOUT_MS
            readTimeout    = READ_TIMEOUT_MS
            requestMethod  = "GET"
        }
        try {
            if (conn.responseCode != HttpURLConnection.HTTP_OK)
                throw RuntimeException("HTTP ${conn.responseCode}")

            val total = if (conn.contentLengthLong > 0) conn.contentLengthLong else expectedBytes
            var downloaded = 0L
            conn.inputStream.use { ins ->
                FileOutputStream(dest).use { fos ->
                    val buf = ByteArray(16 * 1024)
                    var n: Int
                    while (ins.read(buf).also { n = it } != -1) {
                        fos.write(buf, 0, n)
                        downloaded += n
                        onProgress?.invoke(downloaded, total)
                    }
                }
            }
            Log.d(TAG, "Downloaded ${dest.name}: ${downloaded}B / ${total}B")
        } finally {
            conn.disconnect()
        }
    }
}
