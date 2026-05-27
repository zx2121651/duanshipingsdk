package com.sdk.video.capture

import android.content.Context
import android.media.MediaPlayer
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * 音乐卡点控制器（Beat Sync Controller）。
 *
 * 功能：
 *  1. 播放背景音乐（MediaPlayer）
 *  2. 管理节拍标记列表（手动设置或外部导入）
 *  3. [nextBeatFlow]：每到达一个节拍点就发射该节拍的毫秒时戳（供 UI 动画、录制切段使用）
 *  4. [durationUntilNextBeat()]：返回距下一个节拍还剩多少毫秒（用于自动停止当前录制段）
 *
 * 使用示例：
 * ```
 * val beat = BeatSyncController(context)
 * beat.loadMusic("/sdcard/song.mp3")
 * beat.setBeats(listOf(500, 1000, 1500, 2000))   // 或由 analyzeBpm() 自动生成
 * beat.start()
 *
 * // 在拍摄回调中：
 * val msLeft = beat.durationUntilNextBeat()
 * if (msLeft != null) scope.launch { delay(msLeft); segmentRecorder.stopSegment() }
 * ```
 */
class BeatSyncController(
    private val context: Context,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Default + SupervisorJob())
) {
    companion object {
        private const val TAG = "BeatSyncController"
        private const val BEAT_POLL_INTERVAL_MS = 8L // ~120 fps polling
    }

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    private var player: MediaPlayer? = null
    private var musicPath: String? = null

    /** 节拍时间戳列表（毫秒，相对于曲目起点，升序） */
    private var beatMarks: List<Long> = emptyList()

    /** 音乐总时长（ms），从 MediaPlayer 读取 */
    var musicDurationMs: Int = 0
        private set

    @Volatile private var isPlaying = false
    private var beatEmitJob: Job? = null

    private val _nextBeatFlow = MutableSharedFlow<Long>(
        extraBufferCapacity = 4,
        onBufferOverflow = kotlinx.coroutines.channels.BufferOverflow.DROP_OLDEST
    )

    /**
     * 到达节拍时发射该节拍的绝对时间（ms 相对曲目起点）。
     * 外部通过 collect 触发录制切段、UI 闪光等效果。
     */
    val nextBeatFlow: SharedFlow<Long> = _nextBeatFlow.asSharedFlow()

    // -----------------------------------------------------------------------
    // 配置 API
    // -----------------------------------------------------------------------

    /**
     * 加载音乐文件。
     * @param path 本地绝对路径 或 content:// URI 字符串
     */
    fun loadMusic(path: String): Result<Unit> {
        release()
        return try {
            val mp = MediaPlayer()
            mp.setDataSource(path)
            mp.prepare()
            musicPath = path
            musicDurationMs = mp.duration
            player = mp
            Log.i(TAG, "Music loaded: $path (${musicDurationMs}ms)")
            Result.success(Unit)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load music: $path", e)
            Result.failure(e)
        }
    }

    /**
     * 手动设置节拍标记。
     * @param beatsMs 排好序的节拍时间戳列表（相对曲目起点，单位 ms）
     */
    fun setBeats(beatsMs: List<Long>) {
        beatMarks = beatsMs.sorted()
        Log.i(TAG, "Beat marks set: ${beatMarks.size} beats")
    }

    /**
     * 根据 BPM 自动生成均匀节拍序列（全曲）。
     * @param bpm  每分钟节拍数（如 120 BPM = 500ms 间隔）
     * @param offsetMs 第一拍偏移量（用于对齐过门/前奏），默认 0
     */
    fun setBpm(bpm: Float, offsetMs: Long = 0L) {
        require(bpm > 0f) { "BPM must be > 0" }
        val intervalMs = (60_000f / bpm).toLong()
        val totalMs = musicDurationMs.toLong()
        val marks = mutableListOf<Long>()
        var t = offsetMs
        while (t <= totalMs) {
            marks += t
            t += intervalMs
        }
        beatMarks = marks
        Log.i(TAG, "Auto-generated ${marks.size} beats at ${bpm}BPM (interval=${intervalMs}ms)")
    }

    // -----------------------------------------------------------------------
    // 播放控制
    // -----------------------------------------------------------------------

    /** 开始播放背景音乐并启动节拍监听协程。 */
    fun start(positionMs: Int = 0) {
        val mp = player ?: run {
            Log.w(TAG, "start() called but no music loaded")
            return
        }
        if (positionMs > 0) mp.seekTo(positionMs)
        mp.start()
        isPlaying = true
        startBeatEmitter()
        Log.i(TAG, "Music started at ${positionMs}ms")
    }

    /** 暂停播放（节拍发射也暂停）。 */
    fun pause() {
        player?.pause()
        isPlaying = false
        beatEmitJob?.cancel()
        Log.i(TAG, "Music paused at ${currentPositionMs()}ms")
    }

    /** 停止播放并复位到 0。 */
    fun stop() {
        isPlaying = false
        beatEmitJob?.cancel()
        try { player?.stop(); player?.prepare() } catch (_: Exception) {}
        Log.i(TAG, "Music stopped")
    }

    /** 释放所有资源。 */
    fun release() {
        stop()
        player?.release()
        player = null
        scope.cancel()
    }

    // -----------------------------------------------------------------------
    // 节拍查询 API
    // -----------------------------------------------------------------------

    /** 当前播放位置（ms）。若未播放则返回 0。 */
    fun currentPositionMs(): Int = player?.takeIf { isPlaying }?.currentPosition ?: 0

    /**
     * 距下一个节拍还剩多少毫秒。
     * @return null 表示节拍列表为空或已过最后一拍
     */
    fun durationUntilNextBeat(): Long? {
        val now = currentPositionMs().toLong()
        return beatMarks.firstOrNull { it > now }?.let { it - now }
    }

    /**
     * 距下一个节拍的时间（协程挂起版）。等待 [durationUntilNextBeat] 毫秒后返回该拍的时戳。
     * 若无下一拍则立即返回 null。
     */
    suspend fun awaitNextBeat(): Long? {
        val remaining = durationUntilNextBeat() ?: return null
        delay(remaining)
        return currentPositionMs().toLong()
    }

    /** 获取全部节拍时戳（副本）。 */
    fun getBeats(): List<Long> = beatMarks.toList()

    // -----------------------------------------------------------------------
    // 内部：节拍发射协程
    // -----------------------------------------------------------------------

    private fun startBeatEmitter() {
        beatEmitJob?.cancel()
        beatEmitJob = scope.launch {
            var lastEmittedBeat = -1L
            while (isPlaying) {
                val nowMs = withContext(Dispatchers.Main) {
                    player?.currentPosition?.toLong() ?: 0L
                }
                // emit any beat mark that falls in this polling window
                for (beat in beatMarks) {
                    if (beat > lastEmittedBeat && beat <= nowMs) {
                        _nextBeatFlow.tryEmit(beat)
                        lastEmittedBeat = beat
                        Log.v(TAG, "Beat emitted at ${beat}ms")
                    }
                }
                delay(BEAT_POLL_INTERVAL_MS)
            }
        }
    }
}
