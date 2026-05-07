package com.sdk.video.capture

import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import android.util.Log
import com.sdk.video.InternalApi
import com.sdk.video.VideoExportConfig
import com.sdk.video.VideoFilterManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.withContext
import java.io.File
import java.nio.ByteBuffer

/**
 * 分段录制控制器（Segment Recorder）。
 *
 * 使用方式：
 *   1. startSegment()  ← 用户按下录制键
 *   2. stopSegment()   ← 用户松开录制键（可重复多次）
 *   3. deleteLastSegment() ← 用户撤销最后一段（可选）
 *   4. finalize()      ← 用户点击完成，合并所有片段到 outputPath
 *
 * 每段录制委托给 [VideoFilterManager.startVideoRecording] / [stopVideoRecording]，
 * 分段临时文件命名规则：`<outputBase>_seg<N>.mp4`。
 *
 * finalize() 使用 MediaExtractor + MediaMuxer 无损合并，按 PTS 顺序拼接。
 */
@OptIn(InternalApi::class)
class SegmentRecorder(
    private val filterManager: VideoFilterManager,
    private val baseConfig: VideoExportConfig,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
) {
    companion object {
        private const val TAG = "SegmentRecorder"
        private const val MAX_MERGE_BUFFER = 1024 * 512  // 512 KB read buffer
    }

    /** 已完成的单段元数据 */
    data class RecordSegment(
        val filePath: String,
        val durationMs: Long,
        val index: Int
    )

    private val _segments = mutableListOf<RecordSegment>()
    private var segmentStartMs = 0L

    /** 已完成的分段列表（只读快照）*/
    val segments: List<RecordSegment> get() = _segments.toList()

    /** 已录制的分段总数 */
    val segmentCount: Int get() = _segments.size

    /** 所有已完成分段的累计时长（ms） */
    val totalDurationMs: Long get() = _segments.sumOf { it.durationMs }

    /** 当前是否处于录制中 */
    @Volatile var isRecording: Boolean = false
        private set

    // -----------------------------------------------------------------------
    // 公共 API
    // -----------------------------------------------------------------------

    /**
     * 开始录制新的一段。调用前必须确保 GL 线程已绑定 EGL 上下文。
     * @return 失败时包含异常；成功时 Unit
     */
    suspend fun startSegment(): Result<Unit> {
        if (isRecording) {
            return Result.failure(IllegalStateException("Already recording segment ${_segments.size}"))
        }
        val segConfig = baseConfig.copy(outputPath = segTempPath(_segments.size))
        val result = filterManager.startVideoRecording(segConfig)
        if (result.isSuccess) {
            isRecording = true
            segmentStartMs = System.currentTimeMillis()
            Log.i(TAG, "Segment ${_segments.size} started -> ${segConfig.outputPath}")
        }
        return result
    }

    /**
     * 停止当前分段录制并将其加入列表。
     * @return 成功时包含新段的元数据
     */
    suspend fun stopSegment(): Result<RecordSegment> {
        if (!isRecording) {
            return Result.failure(IllegalStateException("Not recording"))
        }
        val durationMs = System.currentTimeMillis() - segmentStartMs
        val result = filterManager.stopVideoRecording()
        isRecording = false
        return result.map {
            val seg = RecordSegment(
                filePath = segTempPath(_segments.size),
                durationMs = durationMs,
                index = _segments.size
            )
            _segments += seg
            Log.i(TAG, "Segment ${seg.index} done: ${durationMs}ms -> ${seg.filePath}")
            seg
        }
    }

    /**
     * 删除最后一段（TikTok 式撤销）。若正在录制中则先停止再删除。
     */
    suspend fun deleteLastSegment() {
        if (isRecording) {
            filterManager.stopVideoRecording()
            isRecording = false
        }
        if (_segments.isEmpty()) return
        val last = _segments.removeLast()
        File(last.filePath).delete()
        Log.i(TAG, "Deleted segment ${last.index}: ${last.filePath}")
    }

    /**
     * 合并所有分段到 [baseConfig.outputPath]。
     *
     * - 单段情况：直接重命名，无需重新封装
     * - 多段情况：MediaExtractor 逐段读取 → MediaMuxer 写入，PTS 做偏移累加
     *
     * @param onProgress 进度回调 [0.0, 1.0]
     * @return 成功时为输出文件路径；失败时为异常
     */
    suspend fun finalize(onProgress: (Float) -> Unit = {}): Result<String> =
        withContext(Dispatchers.IO) {
            if (isRecording) {
                filterManager.stopVideoRecording()
                isRecording = false
            }
            val outputPath = baseConfig.outputPath
            when {
                _segments.isEmpty() -> Result.failure(
                    IllegalStateException("No segments to merge")
                )
                _segments.size == 1 -> {
                    File(_segments[0].filePath).renameTo(File(outputPath))
                    onProgress(1f)
                    _segments.clear()
                    Result.success(outputPath)
                }
                else -> mergeSegments(outputPath, onProgress)
            }
        }

    /**
     * 放弃所有已录制的分段并清理临时文件。
     */
    suspend fun cancel() {
        if (isRecording) {
            filterManager.stopVideoRecording()
            isRecording = false
        }
        _segments.forEach { File(it.filePath).delete() }
        _segments.clear()
        Log.i(TAG, "All segments cancelled and cleaned up")
    }

    // -----------------------------------------------------------------------
    // 内部
    // -----------------------------------------------------------------------

    private fun segTempPath(index: Int): String {
        val base = baseConfig.outputPath.removeSuffix(".mp4")
        return "${base}_seg${index}.mp4"
    }

    private fun mergeSegments(outputPath: String, onProgress: (Float) -> Unit): Result<String> {
        return try {
            val muxer = MediaMuxer(outputPath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
            val buf = ByteBuffer.allocate(MAX_MERGE_BUFFER)

            var videoTrackOut = -1
            var audioTrackOut = -1
            var muxerStarted = false
            var videoPtsOffsetUs = 0L
            var audioPtsOffsetUs = 0L
            // estimated frame duration for PTS gap between segments
            val frameDurUs = if (baseConfig.fps > 0) (1_000_000L / baseConfig.fps) else 33_333L

            for ((segIdx, seg) in _segments.withIndex()) {
                val extractor = MediaExtractor()
                extractor.setDataSource(seg.filePath)

                var segVideoTrack = -1
                var segAudioTrack = -1

                // — collect track formats on first segment only —
                for (i in 0 until extractor.trackCount) {
                    val fmt = extractor.getTrackFormat(i)
                    val mime = fmt.getString(MediaFormat.KEY_MIME) ?: continue
                    when {
                        mime.startsWith("video/") && segVideoTrack < 0 -> {
                            segVideoTrack = i
                            if (!muxerStarted) videoTrackOut = muxer.addTrack(fmt)
                        }
                        mime.startsWith("audio/") && segAudioTrack < 0 -> {
                            segAudioTrack = i
                            if (!muxerStarted) audioTrackOut = muxer.addTrack(fmt)
                        }
                    }
                }

                if (!muxerStarted) {
                    muxer.start()
                    muxerStarted = true
                }

                var maxVideoPtsUs = 0L
                var maxAudioPtsUs = 0L

                // — copy video samples —
                if (segVideoTrack >= 0 && videoTrackOut >= 0) {
                    extractor.selectTrack(segVideoTrack)
                    val info = MediaCodec.BufferInfo()
                    while (true) {
                        buf.clear()
                        val sz = extractor.readSampleData(buf, 0)
                        if (sz < 0) break
                        val rawPts = extractor.sampleTime
                        info.offset = 0
                        info.size = sz
                        info.presentationTimeUs = rawPts + videoPtsOffsetUs
                        info.flags = extractor.sampleFlags
                        maxVideoPtsUs = maxOf(maxVideoPtsUs, rawPts)
                        muxer.writeSampleData(videoTrackOut, buf, info)
                        extractor.advance()
                    }
                    extractor.unselectTrack(segVideoTrack)
                }

                // — copy audio samples —
                if (segAudioTrack >= 0 && audioTrackOut >= 0) {
                    extractor.selectTrack(segAudioTrack)
                    val info = MediaCodec.BufferInfo()
                    while (true) {
                        buf.clear()
                        val sz = extractor.readSampleData(buf, 0)
                        if (sz < 0) break
                        val rawPts = extractor.sampleTime
                        info.offset = 0
                        info.size = sz
                        info.presentationTimeUs = rawPts + audioPtsOffsetUs
                        info.flags = extractor.sampleFlags
                        maxAudioPtsUs = maxOf(maxAudioPtsUs, rawPts)
                        muxer.writeSampleData(audioTrackOut, buf, info)
                        extractor.advance()
                    }
                    extractor.unselectTrack(segAudioTrack)
                }

                extractor.release()

                // advance PTS offsets so next segment starts right after this one
                videoPtsOffsetUs += maxVideoPtsUs + frameDurUs
                audioPtsOffsetUs += maxAudioPtsUs + frameDurUs

                onProgress((segIdx + 1f) / _segments.size)
                Log.i(TAG, "Merged segment $segIdx (video maxPts=${maxVideoPtsUs}us)")
            }

            muxer.stop()
            muxer.release()

            // clean up temp files
            _segments.forEach { File(it.filePath).delete() }
            _segments.clear()

            Log.i(TAG, "Merge complete -> $outputPath")
            Result.success(outputPath)
        } catch (e: Exception) {
            Log.e(TAG, "Merge failed", e)
            Result.failure(e)
        }
    }
}
