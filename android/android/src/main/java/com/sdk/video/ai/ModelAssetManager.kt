package com.sdk.video.ai

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

/**
 * ModelAssetManager — TFLite 模型资产管理器
 *
 * 职责：
 *  - 将打包在 APK `assets/models/` 目录下的 `.tflite` 文件解压到应用私有存储
 *    (`filesDir/models/`)，使 C++ 层能通过绝对路径 `loadModel()` 加载。
 *  - 缓存已解压文件（跳过版本未变更的重复解压）。
 *  - 校验文件非空，对占位 stub 返回 [ModelLoadResult.Stub]，引导开发者替换真实模型。
 *
 * ## 典型用法
 * ```kotlin
 * val mgr = ModelAssetManager(context)
 * when (val r = mgr.extractModel("models/face_landmark_stub.tflite")) {
 *     is ModelLoadResult.Ready -> renderEngine.loadFaceLandmarkModel(r.path)
 *     is ModelLoadResult.Stub  -> Log.w("AI", "Replace ${r.assetName} with a real model")
 *     is ModelLoadResult.Error -> Log.e("AI", r.message)
 * }
 * ```
 */
class ModelAssetManager(private val context: Context) {

    /** Result type returned by [extractModel]. */
    sealed class ModelLoadResult {
        /** Model extracted successfully; [path] is the absolute file path. */
        data class Ready(val path: String, val assetName: String) : ModelLoadResult()

        /**
         * The asset exists but is identified as a stub (starts with [STUB_MAGIC]).
         * Replace the file in `assets/models/` with a real `.tflite` before shipping.
         */
        data class Stub(val assetName: String) : ModelLoadResult()

        /** Extraction failed (missing asset, I/O error, etc.). */
        data class Error(val assetName: String, val message: String) : ModelLoadResult()
    }

    companion object {
        private const val TAG = "ModelAssetManager"

        /** Marker prefix written into placeholder stub files. */
        private const val STUB_MAGIC = "STUB_TFLITE_MODEL"

        /** Destination directory inside app private storage. */
        private const val MODELS_DIR = "models"
    }

    private val modelsDir: File by lazy {
        File(context.filesDir, MODELS_DIR).also { it.mkdirs() }
    }

    /**
     * Extracts [assetName] from APK assets to private storage and returns the result.
     *
     * This function is safe to call from any coroutine; it switches to [Dispatchers.IO]
     * for the file copy so it will not block the Main thread.
     *
     * @param assetName  Relative path inside `assets/`, e.g. `"models/face_landmark_stub.tflite"`
     * @param forceRefresh  When `true`, always overwrite even if the destination exists.
     */
    suspend fun extractModel(
        assetName: String,
        forceRefresh: Boolean = false
    ): ModelLoadResult = withContext(Dispatchers.IO) {
        val fileName  = File(assetName).name
        val destFile  = File(modelsDir, fileName)

        try {
            if (!forceRefresh && destFile.exists() && destFile.length() > 0L) {
                return@withContext classify(destFile, assetName)
            }

            context.assets.open(assetName).use { input ->
                FileOutputStream(destFile).use { output ->
                    input.copyTo(output)
                }
            }
            Log.d(TAG, "Extracted $assetName → ${destFile.absolutePath} (${destFile.length()} bytes)")
            classify(destFile, assetName)

        } catch (e: IOException) {
            val msg = "Failed to extract asset '$assetName': ${e.message}"
            Log.e(TAG, msg, e)
            ModelLoadResult.Error(assetName, msg)
        }
    }

    /**
     * Synchronous extraction — only use from a background thread (e.g., inside a CoroutineWorker).
     */
    fun extractModelSync(assetName: String, forceRefresh: Boolean = false): ModelLoadResult {
        val fileName = File(assetName).name
        val destFile = File(modelsDir, fileName)

        return try {
            if (!forceRefresh && destFile.exists() && destFile.length() > 0L) {
                return classify(destFile, assetName)
            }
            context.assets.open(assetName).use { input ->
                FileOutputStream(destFile).use { output -> input.copyTo(output) }
            }
            classify(destFile, assetName)
        } catch (e: IOException) {
            ModelLoadResult.Error(assetName, "I/O error: ${e.message}")
        }
    }

    /** Returns the cached destination path if already extracted, null otherwise. */
    fun getCachedPath(assetName: String): String? {
        val dest = File(modelsDir, File(assetName).name)
        return if (dest.exists() && dest.length() > 0) dest.absolutePath else null
    }

    /** Deletes all cached model files from private storage. */
    fun clearCache() {
        modelsDir.listFiles()?.forEach { it.delete() }
        Log.i(TAG, "Model cache cleared")
    }

    // ── private helpers ──────────────────────────────────────────────────────

    private fun classify(file: File, assetName: String): ModelLoadResult {
        if (!file.exists() || file.length() == 0L) {
            return ModelLoadResult.Error(assetName, "Destination file is empty after extraction")
        }
        // Peek at the first few bytes to detect stub files
        val header = file.inputStream().bufferedReader().use { it.readLine() ?: "" }
        return if (header.startsWith(STUB_MAGIC)) {
            Log.w(TAG, "[$assetName] is a STUB placeholder. Replace with a real .tflite model.")
            ModelLoadResult.Stub(assetName)
        } else {
            ModelLoadResult.Ready(file.absolutePath, assetName)
        }
    }
}
