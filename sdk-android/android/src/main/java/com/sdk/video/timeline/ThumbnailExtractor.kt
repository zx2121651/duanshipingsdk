package com.sdk.video.timeline

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.util.Log
import android.util.LruCache
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest

/**
 * 视频缩略图提取器（Timeline 编辑器缩略图轨道专用）。
 *
 * ## 双层缓存策略
 * 1. **内存缓存**（LruCache，默认 30MB）：热点帧直接复用，无 I/O。
 * 2. **磁盘缓存**（JPEG 80，cacheDir/thumbnails/）：跨会话复用，避免重复解码。
 *
 * ## 两种提取模式
 * - **Sync（I-frame nearest）**：`MediaMetadataRetriever.OPTION_CLOSEST_SYNC`，
 *   速度快，误差约 ±0.5s，适合轨道缩略图网格。
 * - **Exact**：`MediaMetadataRetriever.OPTION_CLOSEST`，
 *   帧精确，速度慢（需要解码多帧），仅在 seek-end 时使用。
 *
 * ## 线程安全
 * 所有挂起函数在 `Dispatchers.IO` 上执行；内存缓存通过 `LruCache` 内建同步。
 */
class ThumbnailExtractor(context: Context) {

    companion object {
        private const val TAG = "ThumbnailExtractor"
        private const val JPEG_QUALITY = 80
        private const val DISK_CACHE_DIR = "thumbnails"
    }

    private val diskCacheDir: File =
        File(context.cacheDir, DISK_CACHE_DIR).also { it.mkdirs() }

    // LRU 内存缓存：按 JVM 堆上的 Bitmap 字节数计量
    private val memCache = object : LruCache<String, Bitmap>(30 * 1024 * 1024) {
        override fun sizeOf(key: String, value: Bitmap) = value.byteCount
    }

    // ── 公开 API ──────────────────────────────────────────────────────────────

    /**
     * 提取缩略图（异步，优先从缓存读取）。
     *
     * @param path      视频文件绝对路径或 content:// URI 字符串
     * @param posMs     目标时间点（毫秒）
     * @param width     目标宽度（像素），0 = 原始宽
     * @param height    目标高度（像素），0 = 原始高
     * @param exact     true = 帧精确（慢），false = I 帧近似（快）
     */
    suspend fun extractAt(
        path: String,
        posMs: Long,
        width: Int = 128,
        height: Int = 72,
        exact: Boolean = false
    ): Bitmap? = withContext(Dispatchers.IO) {
        val cacheKey = cacheKey(path, posMs, width, height)

        // 1. 内存缓存
        memCache.get(cacheKey)?.let { return@withContext it }

        // 2. 磁盘缓存
        val diskFile = File(diskCacheDir, "$cacheKey.jpg")
        if (diskFile.exists()) {
            val opts = BitmapFactory.Options().apply {
                inPreferredConfig = Bitmap.Config.RGB_565 // 减半内存
            }
            BitmapFactory.decodeFile(diskFile.absolutePath, opts)?.let { bm ->
                memCache.put(cacheKey, bm)
                return@withContext bm
            }
        }

        // 3. 解码
        val bm = decodeFrame(path, posMs, width, height, exact) ?: return@withContext null

        // 写磁盘缓存
        try {
            FileOutputStream(diskFile).use { fos ->
                bm.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, fos)
            }
        } catch (e: Exception) {
            Log.w(TAG, "Disk cache write failed for $cacheKey", e)
        }
        memCache.put(cacheKey, bm)
        bm
    }

    /**
     * 批量提取缩略图网格（均匀采样，用于轨道时间线渲染）。
     *
     * @param path      视频路径
     * @param count     缩略图数量（通常 = 轨道宽度 / 缩略图宽度）
     * @param totalMs   视频总时长（毫秒）
     * @param width     单张缩略图宽（像素）
     * @param height    单张缩略图高（像素）
     */
    suspend fun extractGrid(
        path: String,
        count: Int,
        totalMs: Long,
        width: Int = 128,
        height: Int = 72
    ): List<Bitmap?> = withContext(Dispatchers.IO) {
        if (count <= 0 || totalMs <= 0) return@withContext emptyList()
        val step = totalMs / count
        (0 until count).map { i ->
            extractAt(path, i * step, width, height, exact = false)
        }
    }

    /** 清除内存缓存（GL context 丢失时调用）。 */
    fun clearMemoryCache() = memCache.evictAll()

    /** 删除全部磁盘缓存。 */
    fun clearDiskCache() {
        diskCacheDir.listFiles()?.forEach { it.delete() }
    }

    // ── 私有实现 ──────────────────────────────────────────────────────────────

    private fun decodeFrame(
        path: String,
        posMs: Long,
        width: Int,
        height: Int,
        exact: Boolean
    ): Bitmap? {
        val retriever = MediaMetadataRetriever()
        return try {
            if (path.startsWith("content://")) {
                // content URI handled by caller via openFileDescriptor before passing here
                retriever.setDataSource(path, emptyMap())
            } else {
                retriever.setDataSource(path)
            }
            val option = if (exact)
                MediaMetadataRetriever.OPTION_CLOSEST
            else
                MediaMetadataRetriever.OPTION_CLOSEST_SYNC

            val raw: Bitmap = retriever.getFrameAtTime(posMs * 1000L, option)
                ?: return null

            if (width <= 0 || height <= 0) return raw
            scaleBitmap(raw, width, height)
        } catch (e: Exception) {
            Log.w(TAG, "decodeFrame failed path=$path pos=$posMs", e)
            null
        } finally {
            retriever.release()
        }
    }

    private fun scaleBitmap(src: Bitmap, targetW: Int, targetH: Int): Bitmap {
        if (src.width == targetW && src.height == targetH) return src
        // Letterbox-fit: scale keeping aspect ratio, then center-crop
        val srcRatio  = src.width.toFloat() / src.height
        val dstRatio  = targetW.toFloat() / targetH
        val (sw, sh) = if (srcRatio > dstRatio) {
            (src.height * dstRatio).toInt() to src.height
        } else {
            src.width to (src.width / dstRatio).toInt()
        }
        val x = (src.width  - sw) / 2
        val y = (src.height - sh) / 2
        val cropped = Bitmap.createBitmap(src, x, y, sw, sh)
        val scaled  = Bitmap.createScaledBitmap(cropped, targetW, targetH, true)
        if (cropped !== src) cropped.recycle()
        return scaled
    }

    private fun cacheKey(path: String, posMs: Long, w: Int, h: Int): String {
        val raw = "$path@${posMs / 1000}s_${w}x${h}" // 1-second granularity
        return MessageDigest.getInstance("MD5")
            .digest(raw.toByteArray())
            .joinToString("") { "%02x".format(it) }
    }
}
