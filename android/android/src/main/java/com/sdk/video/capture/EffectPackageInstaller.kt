package com.sdk.video.capture

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * 特效资源包安装器（Effect Package Installer）。
 *
 * 资源包协议（.bundle 格式）：
 *   特效包是一个标准 ZIP 文件，扩展名为 `.bundle`，内部必须包含：
 *
 *   my_effect.bundle/
 *     manifest.json          ← 特效描述（必须）
 *     textures/              ← PNG/WebP RGBA 贴纸纹理
 *       sticker_0.png
 *       frames/              ← 动画帧序列（可选）
 *         frame_000.png
 *         frame_001.png
 *     luts/                  ← .cube 颜色 LUT（可选）
 *       color.cube
 *     models/                ← TFLite 模型（可选）
 *       landmark.tflite
 *
 * manifest.json 完整 schema（v1.1）：
 * ```json
 * {
 *   "id": "my_effect",
 *   "name": "我的特效",
 *   "version": "1.0",
 *   "schema_version": "1.1",
 *   "type": "face_sticker",
 *   "layers": [
 *     {
 *       "type": "sticker",
 *       "texture": "textures/sticker_0.png",
 *       "anchor": "forehead",
 *       "anchor_type": "face",
 *       "scale": 0.25,
 *       "offset_x": 0.0,
 *       "offset_y": -0.05,
 *       "track_rotation": true,
 *       "frames": ["textures/frames/frame_000.png", "textures/frames/frame_001.png"],
 *       "frame_rate": 24,
 *       "loop": true,
 *       "gesture": ""
 *     },
 *     {
 *       "type": "sticker",
 *       "texture": "textures/heart.png",
 *       "anchor": "left_wrist",
 *       "anchor_type": "body",
 *       "scale": 0.15,
 *       "gesture": "heart_hands"
 *     },
 *     {
 *       "type": "beauty",
 *       "eyeScale": 0.3,
 *       "faceSlim": 0.2
 *     }
 *   ]
 * }
 * ```
 *
 * 安装结果：解压到 [context.cacheDir]/effects/<effectId>/，
 * 并返回该目录路径供 VideoFilterManager.loadEffect() 使用。
 */
object EffectPackageInstaller {

    private const val TAG = "EffectPackageInstaller"
    private const val EFFECTS_CACHE_DIR = "effects"
    private const val MANIFEST_FILENAME  = "manifest.json"
    private const val BUNDLE_EXTENSION   = ".bundle"

    /**
     * 解压 .bundle 文件到缓存目录。
     *
     * @param context  Android Context
     * @param bundleStream  .bundle 文件的 InputStream（来自 AssetManager 或文件系统）
     * @param packageName   包名（不含扩展名），用于缓存子目录命名
     * @return 解压后的目录绝对路径；失败时为空字符串
     */
    suspend fun install(
        context: Context,
        bundleStream: InputStream,
        packageName: String
    ): Result<String> = withContext(Dispatchers.IO) {
        val outputDir = File(context.cacheDir, "$EFFECTS_CACHE_DIR/$packageName")
        return@withContext try {
            // Clean and recreate output directory
            if (outputDir.exists()) outputDir.deleteRecursively()
            outputDir.mkdirs()

            val buf = ByteArray(8192)
            ZipInputStream(bundleStream.buffered()).use { zip ->
                var entry = zip.nextEntry
                while (entry != null) {
                    val outFile = File(outputDir, entry.name)
                    if (entry.isDirectory) {
                        outFile.mkdirs()
                    } else {
                        outFile.parentFile?.mkdirs()
                        FileOutputStream(outFile).use { out ->
                            var n: Int
                            while (zip.read(buf).also { n = it } != -1)
                                out.write(buf, 0, n)
                        }
                    }
                    zip.closeEntry()
                    entry = zip.nextEntry
                }
            }

            // Validate: manifest.json must exist
            val manifest = File(outputDir, MANIFEST_FILENAME)
            if (!manifest.exists()) {
                outputDir.deleteRecursively()
                return@withContext Result.failure(
                    IllegalArgumentException("$packageName.bundle is missing manifest.json")
                )
            }

            Log.i(TAG, "Installed effect package '$packageName' -> ${outputDir.absolutePath}")
            Result.success(outputDir.absolutePath)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to install effect package '$packageName'", e)
            outputDir.deleteRecursively()
            Result.failure(e)
        }
    }

    /**
     * 从 Android assets/ 目录安装 .bundle。
     *
     * @param assetPath  assets 内的路径，例如 "effects/my_effect.bundle"
     */
    suspend fun installFromAssets(
        context: Context,
        assetPath: String
    ): Result<String> {
        val packageName = File(assetPath).nameWithoutExtension
        return try {
            val stream = context.assets.open(assetPath)
            install(context, stream, packageName)
        } catch (e: Exception) {
            Log.e(TAG, "Asset not found: $assetPath", e)
            Result.failure(e)
        }
    }

    /**
     * 从本地文件系统安装 .bundle。
     *
     * @param bundlePath  .bundle 文件的绝对路径
     */
    suspend fun installFromFile(
        context: Context,
        bundlePath: String
    ): Result<String> {
        val file = File(bundlePath)
        if (!file.exists() || !file.name.endsWith(BUNDLE_EXTENSION, ignoreCase = true)) {
            return Result.failure(IllegalArgumentException("Not a .bundle file: $bundlePath"))
        }
        val packageName = file.nameWithoutExtension
        return try {
            install(context, file.inputStream(), packageName)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to open bundle file: $bundlePath", e)
            Result.failure(e)
        }
    }

    /**
     * 删除已安装的特效缓存目录。
     *
     * @param packageName  包名（不含扩展名）
     */
    fun uninstall(context: Context, packageName: String) {
        val dir = File(context.cacheDir, "$EFFECTS_CACHE_DIR/$packageName")
        if (dir.exists()) {
            dir.deleteRecursively()
            Log.i(TAG, "Uninstalled effect package: $packageName")
        }
    }

    /**
     * 列出所有已安装的特效包名。
     */
    fun installedPackages(context: Context): List<String> {
        val dir = File(context.cacheDir, EFFECTS_CACHE_DIR)
        return dir.listFiles()
            ?.filter { it.isDirectory && File(it, MANIFEST_FILENAME).exists() }
            ?.map { it.name }
            ?: emptyList()
    }

    /**
     * 检查特效包是否已安装（manifest.json 存在即视为有效）。
     */
    fun isInstalled(context: Context, packageName: String): Boolean {
        return File(context.cacheDir, "$EFFECTS_CACHE_DIR/$packageName/$MANIFEST_FILENAME").exists()
    }

    /**
     * 返回已安装包的根目录路径（不检查有效性）。
     */
    fun packageDir(context: Context, packageName: String): String =
        File(context.cacheDir, "$EFFECTS_CACHE_DIR/$packageName").absolutePath
}
