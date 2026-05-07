package com.sdk.video.sample.ui

import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * TimelinePlayer — 协程驱动的播放时钟
 *
 * 以 [TARGET_FPS] 帧率驱动 [positionUs]（微秒），支持 play / pause / seek。
 * 不持有 GL 资源，仅负责时间线位置推进；[TimelinePreviewSurface] 监听
 * [positionUs] 并在 GL 线程拉取对应帧。
 *
 * ## 典型用法（ViewModel 中）
 * ```kotlin
 * val player = TimelinePlayer(viewModelScope, totalDurationUs)
 * player.play()
 * // 在 Composable 中：
 * val pos by player.positionUs.collectAsState()
 * ```
 */
class TimelinePlayer(
    private val scope: CoroutineScope,
    totalDurationUs: Long
) {
    companion object {
        const val TARGET_FPS = 30
        private const val FRAME_US = 1_000_000L / TARGET_FPS
    }

    private val _positionUs   = MutableStateFlow(0L)
    val positionUs: StateFlow<Long> = _positionUs.asStateFlow()

    private val _isPlaying    = MutableStateFlow(false)
    val isPlaying: StateFlow<Boolean> = _isPlaying.asStateFlow()

    private val _totalDurUs   = MutableStateFlow(totalDurationUs)
    val totalDurationUs: StateFlow<Long> = _totalDurUs.asStateFlow()

    private var playJob: Job? = null

    /** Update total duration when clips change. */
    fun setTotalDuration(durationUs: Long) {
        _totalDurUs.value = durationUs
    }

    /** Start / resume playback. No-op if already playing. */
    fun play() {
        if (_isPlaying.value) return
        _isPlaying.value = true
        playJob = scope.launch {
            val frameDeltaNs = FRAME_US * 1_000L  // convert to ns for delay
            while (isActive) {
                val beforeNs = System.nanoTime()
                val newPos = _positionUs.value + FRAME_US
                if (newPos >= _totalDurUs.value) {
                    _positionUs.value = _totalDurUs.value
                    _isPlaying.value  = false
                    break
                }
                _positionUs.value = newPos
                val elapsedNs = System.nanoTime() - beforeNs
                val sleepMs   = ((frameDeltaNs - elapsedNs) / 1_000_000L).coerceAtLeast(1L)
                delay(sleepMs)
            }
        }
    }

    /** Pause playback. No-op if already paused. */
    fun pause() {
        if (!_isPlaying.value) return
        _isPlaying.value = false
        playJob?.cancel()
        playJob = null
    }

    /** Toggle play/pause. */
    fun togglePlayPause() {
        if (_isPlaying.value) pause() else play()
    }

    /** Seek to [positionUs] (clamped to [0, totalDurationUs]). */
    fun seekTo(positionUs: Long) {
        val clamped = positionUs.coerceIn(0L, _totalDurUs.value)
        _positionUs.value = clamped
    }

    /** Reset position to 0 and stop playback. */
    fun reset() {
        pause()
        _positionUs.value = 0L
    }

    /** Release resources (cancels any running play job). */
    fun release() {
        playJob?.cancel()
        playJob = null
    }
}
