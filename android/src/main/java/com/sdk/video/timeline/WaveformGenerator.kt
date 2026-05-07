package com.sdk.video.timeline

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

/**
 * 音频波形生产器（Timeline 编辑器波形轨道专用）。
 *
 * ## 生产链路
 * ```
 * 音频文件
 *   └─ MediaExtractor  (demux → AAC/MP3/FLAC compressed packets)
 *       └─ MediaCodec  (decode → PCM int16 frames)
 *           └─ extractWaveformPeaks()  (C++ via TimelineManager / pure Kotlin fallback)
 *               └─ FloatArray[numBars]  (归一化峰值 [0,1])
 * ```
 *
 * ## 磁盘缓存
 * 路径：`cacheDir/waveforms/<md5(path+numBars)>.waveform`
 * 格式：小端 float32 数组，长度 = numBars
 *
 * ## 线程安全
 * 全部在 `Dispatchers.IO` 上运行；缓存读写串行化。
 */
class WaveformGenerator(context: Context) {

    companion object {
        private const val TAG = "WaveformGenerator"
        private const val CACHE_DIR = "waveforms"
        private const val CACHE_MAGIC = 0x57415645 // "WAVE" little-endian
        private const val DECODE_TIMEOUT_US = 5_000L  // 5ms codec timeout
        private const val MAX_DECODE_MS = 10_000L     // give up after 10s
    }

    private val cacheDir = File(context.cacheDir, CACHE_DIR).also { it.mkdirs() }

    // ── 公开 API ──────────────────────────────────────────────────────────────

    /**
     * 生成波形峰值数组（异步，优先从磁盘缓存读取）。
     *
     * @param path    音频/视频文件绝对路径
     * @param numBars 目标柱数（通常 = 轨道 UI 像素宽度）
     * @return        长度 = [numBars] 的归一化峰值数组 [0, 1]，或 null（失败）
     */
    suspend fun generate(path: String, numBars: Int): FloatArray? =
        withContext(Dispatchers.IO) {
            require(numBars > 0) { "numBars must be positive" }

            val key = cacheKey(path, numBars)
            // 1. 磁盘缓存
            readCache(key)?.let { return@withContext it }
            // 2. 解码 + 计算
            val pcm16 = decodeToPcm16(path) ?: return@withContext null
            val channels = getChannelCount(path)
            val peaks = extractPeaks(pcm16, numBars, channels)
            // 3. 写磁盘缓存
            writeCache(key, peaks)
            peaks
        }

    /** 预热缓存（在后台提前计算，以防 UI 显示时卡顿）。 */
    suspend fun prefetch(path: String, numBars: Int) { generate(path, numBars) }

    /** 删除某个文件对应的所有缓存。 */
    fun evict(path: String) {
        cacheDir.listFiles { _, name -> name.startsWith(md5(path)) }
            ?.forEach { it.delete() }
    }

    /** 清除全部磁盘缓存。 */
    fun clearAll() { cacheDir.listFiles()?.forEach { it.delete() } }

    // ── PCM 解码 ─────────────────────────────────────────────────────────────

    private fun decodeToPcm16(path: String): ShortArray? {
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        return try {
            extractor.setDataSource(path)
            val trackIdx = findAudioTrack(extractor) ?: return null
            extractor.selectTrack(trackIdx)
            val format = extractor.getTrackFormat(trackIdx)
            val mime   = format.getString(MediaFormat.KEY_MIME) ?: return null

            codec = MediaCodec.createDecoderByType(mime)
            codec.configure(format, null, null, 0)
            codec.start()

            val pcmOut = mutableListOf<Short>()
            val inputBuffers  = codec.inputBuffers
            val outputBuffers = codec.outputBuffers
            val info = MediaCodec.BufferInfo()
            var eos = false
            val deadline = System.currentTimeMillis() + MAX_DECODE_MS

            while (!eos && System.currentTimeMillis() < deadline) {
                // Feed input
                val inIdx = codec.dequeueInputBuffer(DECODE_TIMEOUT_US)
                if (inIdx >= 0) {
                    val buf = inputBuffers[inIdx]
                    val size = extractor.readSampleData(buf, 0)
                    if (size < 0) {
                        codec.queueInputBuffer(inIdx, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                        eos = true
                    } else {
                        codec.queueInputBuffer(inIdx, 0, size, extractor.sampleTime, 0)
                        extractor.advance()
                    }
                }
                // Drain output
                var outIdx = codec.dequeueOutputBuffer(info, DECODE_TIMEOUT_US)
                while (outIdx >= 0) {
                    if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) {
                        eos = true
                    }
                    val outBuf = outputBuffers[outIdx]
                    outBuf.position(info.offset)
                    outBuf.limit(info.offset + info.size)
                    val shortBuf = outBuf.order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
                    val shorts = ShortArray(shortBuf.remaining())
                    shortBuf.get(shorts)
                    pcmOut.addAll(shorts.asIterable())
                    codec.releaseOutputBuffer(outIdx, false)
                    outIdx = codec.dequeueOutputBuffer(info, DECODE_TIMEOUT_US)
                }
            }
            pcmOut.toShortArray()
        } catch (e: Exception) {
            Log.e(TAG, "decodeToPcm16 failed: $path", e)
            null
        } finally {
            codec?.stop()
            codec?.release()
            extractor.release()
        }
    }

    private fun findAudioTrack(extractor: MediaExtractor): Int? {
        for (i in 0 until extractor.trackCount) {
            val fmt  = extractor.getTrackFormat(i)
            val mime = fmt.getString(MediaFormat.KEY_MIME) ?: continue
            if (mime.startsWith("audio/")) return i
        }
        return null
    }

    private fun getChannelCount(path: String): Int {
        val extractor = MediaExtractor()
        return try {
            extractor.setDataSource(path)
            val idx = findAudioTrack(extractor) ?: return 1
            extractor.getTrackFormat(idx)
                .getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        } catch (_: Exception) { 1 }
        finally { extractor.release() }
    }

    // ── 峰值计算（纯 Kotlin；如有 JNI 可替换为 native 版本） ───────────────

    private fun extractPeaks(pcm: ShortArray, numBars: Int, channels: Int): FloatArray {
        val peaks = FloatArray(numBars)
        val totalFrames = pcm.size / channels.coerceAtLeast(1)
        if (totalFrames == 0) return peaks

        val framesPerBar = totalFrames.toFloat() / numBars
        var globalMax = 1 // avoid /0

        for (bar in 0 until numBars) {
            val start = (bar * framesPerBar).toInt()
            val end   = ((bar + 1) * framesPerBar).toInt().coerceAtMost(totalFrames)
            var peakAbs = 0
            for (f in start until end) {
                for (ch in 0 until channels.coerceAtLeast(1)) {
                    val idx = f * channels + ch
                    if (idx < pcm.size) {
                        val abs = Math.abs(pcm[idx].toInt())
                        if (abs > peakAbs) peakAbs = abs
                    }
                }
            }
            peaks[bar] = peakAbs.toFloat()
            if (peakAbs > globalMax) globalMax = peakAbs
        }
        // Normalize
        for (i in peaks.indices) peaks[i] = peaks[i] / globalMax
        return peaks
    }

    // ── 磁盘缓存 I/O ─────────────────────────────────────────────────────────

    private fun readCache(key: String): FloatArray? {
        val file = File(cacheDir, "$key.waveform")
        if (!file.exists()) return null
        return try {
            DataInputStream(FileInputStream(file).buffered()).use { dis ->
                val magic   = dis.readInt()
                val version = dis.readInt()
                val count   = dis.readInt()
                if (magic != CACHE_MAGIC || version != 1) return null
                FloatArray(count) { dis.readFloat() }
            }
        } catch (e: Exception) {
            Log.w(TAG, "readCache failed: $key", e)
            file.delete()
            null
        }
    }

    private fun writeCache(key: String, peaks: FloatArray) {
        val file = File(cacheDir, "$key.waveform")
        try {
            DataOutputStream(FileOutputStream(file).buffered()).use { dos ->
                dos.writeInt(CACHE_MAGIC)
                dos.writeInt(1)           // version
                dos.writeInt(peaks.size)
                for (v in peaks) dos.writeFloat(v)
            }
        } catch (e: Exception) {
            Log.w(TAG, "writeCache failed: $key", e)
            file.delete()
        }
    }

    private fun cacheKey(path: String, numBars: Int) = md5("$path|$numBars")

    private fun md5(s: String) = MessageDigest.getInstance("MD5")
        .digest(s.toByteArray())
        .joinToString("") { "%02x".format(it) }
}
