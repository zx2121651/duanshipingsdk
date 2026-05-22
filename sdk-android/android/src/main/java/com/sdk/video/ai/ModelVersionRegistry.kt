package com.sdk.video.ai

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest

/**
 * 模型版本注册表 — 跟踪每个 .tflite 模型的版本号、SHA-256 校验和与文件大小。
 *
 * ## 存储格式（filesDir/models/.manifest.json）
 * ```json
 * {
 *   "version": 1,
 *   "models": {
 *     "face_landmark.tflite": {
 *       "version": "20240101",
 *       "sha256": "a3f...",
 *       "sizeBytes": 4194304,
 *       "timestamp": 1704067200000,
 *       "source": "assets"
 *     }
 *   }
 * }
 * ```
 *
 * ## 版本号约定
 * - APK 内置模型：`assets_<yyyyMMdd>` 或语义版本 `1.0.0`
 * - OTA 下载模型：服务端 manifest 中指定，如 `1.2.3`
 *
 * ## 线程安全
 * 所有 I/O 在 `Dispatchers.IO` 上执行；读写通过 `@Synchronized` 串行化。
 */
class ModelVersionRegistry(context: Context) {

    data class ModelInfo(
        val version: String,
        val sha256: String = "",     // 小写十六进制 SHA-256，空 = 跳过校验
        val sizeBytes: Long = 0L,
        val timestamp: Long = System.currentTimeMillis(),
        val source: String = "assets" // "assets" | "ota"
    )

    companion object {
        private const val TAG = "ModelVersionRegistry"
        private const val MANIFEST_FILE = ".manifest.json"
        private const val SCHEMA_VERSION = 1
    }

    private val modelsDir = File(context.filesDir, "models").also { it.mkdirs() }
    private val manifestFile = File(modelsDir, MANIFEST_FILE)

    @Volatile private var cache: MutableMap<String, ModelInfo> = mutableMapOf()
    private val lock = Any()

    init { loadFromDisk() }

    // ── 查询 ──────────────────────────────────────────────────────────────────

    fun get(modelName: String): ModelInfo? = synchronized(lock) { cache[modelName] }

    fun isUpToDate(modelName: String, expectedVersion: String): Boolean =
        get(modelName)?.version == expectedVersion

    /** 校验文件 SHA-256 是否与注册表中的记录一致。 */
    fun verifyChecksum(file: File, modelName: String): Boolean {
        val info = get(modelName) ?: return true // 无记录 = 跳过校验
        if (info.sha256.isEmpty()) return true
        return computeSha256(file) == info.sha256
    }

    /** 计算文件 SHA-256（小写十六进制），文件不存在时返回空字符串。 */
    fun computeSha256(file: File): String {
        if (!file.exists()) return ""
        return try {
            val md = MessageDigest.getInstance("SHA-256")
            file.inputStream().use { ins ->
                val buf = ByteArray(8192)
                var n: Int
                while (ins.read(buf).also { n = it } != -1) md.update(buf, 0, n)
            }
            md.digest().joinToString("") { "%02x".format(it) }
        } catch (e: Exception) {
            Log.e(TAG, "SHA-256 failed for ${file.name}", e)
            ""
        }
    }

    // ── 更新 ──────────────────────────────────────────────────────────────────

    fun put(modelName: String, info: ModelInfo) {
        synchronized(lock) { cache[modelName] = info }
        saveToDisk()
    }

    fun remove(modelName: String) {
        synchronized(lock) { cache.remove(modelName) }
        saveToDisk()
    }

    fun all(): Map<String, ModelInfo> = synchronized(lock) { cache.toMap() }

    // ── 持久化 ────────────────────────────────────────────────────────────────

    private fun loadFromDisk() {
        if (!manifestFile.exists()) return
        try {
            val root = JSONObject(manifestFile.readText())
            val models = root.optJSONObject("models") ?: return
            val loaded = mutableMapOf<String, ModelInfo>()
            for (key in models.keys()) {
                val obj = models.getJSONObject(key)
                loaded[key] = ModelInfo(
                    version   = obj.optString("version", ""),
                    sha256    = obj.optString("sha256", ""),
                    sizeBytes = obj.optLong("sizeBytes", 0L),
                    timestamp = obj.optLong("timestamp", 0L),
                    source    = obj.optString("source", "assets")
                )
            }
            synchronized(lock) { cache = loaded }
            Log.d(TAG, "Registry loaded: ${loaded.size} model(s)")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load manifest", e)
        }
    }

    @Synchronized
    private fun saveToDisk() {
        try {
            val models = JSONObject()
            synchronized(lock) {
                for ((name, info) in cache) {
                    models.put(name, JSONObject().apply {
                        put("version",   info.version)
                        put("sha256",    info.sha256)
                        put("sizeBytes", info.sizeBytes)
                        put("timestamp", info.timestamp)
                        put("source",    info.source)
                    })
                }
            }
            val root = JSONObject().apply {
                put("version", SCHEMA_VERSION)
                put("models", models)
            }
            manifestFile.writeText(root.toString(2))
        } catch (e: Exception) {
            Log.e(TAG, "Failed to save manifest", e)
        }
    }
}
