package com.sdk.video.sample.core.model

import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * 核心播放时钟驱动器 (TimelinePlayer)
 *
 * 这是一个完全基于 Kotlin 协程的高精度软件时钟计数器，专门用于驱动编辑器预览的时间点更新：
 * - 以设定的目标帧率 [TARGET_FPS] 驱动并累加 [positionUs]（单位：微秒）。
 * - 暴露流式状态 [positionUs] 和 [isPlaying]，供各种 Compose UI（如预览界面、滑动时间轴、转场进度）直接进行异步响应式流式绑定。
 * - 该组件不分配或持有任何 OpenGL ES 或硬解码物理资源，只充当同步“发令枪”。物理帧的最终调度由 UI 端（如 GLSurfaceView 或 TextureView）监听位置信号自行拉取。
 *
 * @param scope 绑定生命周期的协程 Scope，通常使用 ViewModel 作用域 viewModelScope
 * @param totalDurationUs 视频总时长（微秒）
 */
class TimelinePlayer(
    private val scope: CoroutineScope,
    totalDurationUs: Long
) {
    companion object {
        const val TARGET_FPS = 30 // 目标预览帧率：30 FPS
        private const val FRAME_US = 1_000_000L / TARGET_FPS // 单帧耗时（微秒）：约 33.33ms
    }

    // 内部播放时间（微秒）状态流
    private val _positionUs   = MutableStateFlow(0L)
    val positionUs: StateFlow<Long> = _positionUs.asStateFlow()

    // 内部播放/暂停状态流
    private val _isPlaying    = MutableStateFlow(false)
    val isPlaying: StateFlow<Boolean> = _isPlaying.asStateFlow()

    // 内部视频总时长（微秒）状态流
    private val _totalDurUs   = MutableStateFlow(totalDurationUs)
    val totalDurationUs: StateFlow<Long> = _totalDurUs.asStateFlow()

    private var playJob: Job? = null

    /**
     * 更新播放总时长
     * 当导入新片段或裁剪时由 ViewModel 触发同步
     */
    fun setTotalDuration(durationUs: Long) {
        _totalDurUs.value = durationUs
    }

    /**
     * 开始播放。如已处于播放状态，此调用直接返回。
     * 内部启动一个非阻塞的高精度循环协程计算每帧偏移量并执行 `delay()` 休眠补偿。
     */
    fun play() {
        if (_isPlaying.value) return
        _isPlaying.value = true
        playJob = scope.launch {
            val frameDeltaNs = FRAME_US * 1_000L  // 换算为纳秒用于高精度差值对齐
            while (isActive) {
                val beforeNs = System.nanoTime()
                val newPos = _positionUs.value + FRAME_US
                
                // 播放越界处理
                if (newPos >= _totalDurUs.value) {
                    _positionUs.value = _totalDurUs.value
                    _isPlaying.value  = false
                    break
                }
                
                _positionUs.value = newPos
                
                // 自动补偿计算消耗：单帧预估时值减去运行消耗时间
                val elapsedNs = System.nanoTime() - beforeNs
                val sleepMs   = ((frameDeltaNs - elapsedNs) / 1_000_000L).coerceAtLeast(1L)
                delay(sleepMs)
            }
        }
    }

    /**
     * 暂停播放。
     * 停止高精度时钟，保留当前微秒指针。
     */
    fun pause() {
        if (!_isPlaying.value) return
        _isPlaying.value = false
        playJob?.cancel()
        playJob = null
    }

    /**
     * 播放/暂停状态反转切换
     */
    fun togglePlayPause() {
        if (_isPlaying.value) pause() else play()
    }

    /**
     * 定位 Seek 到指定时刻
     * 自动拦截小于 0 或超过视频总长度的非合规入参。
     */
    fun seekTo(positionUs: Long) {
        val clamped = positionUs.coerceIn(0L, _totalDurUs.value)
        _positionUs.value = clamped
    }

    /**
     * 重置播放器：停止播放并回滚到首帧 (0 Us)。
     */
    fun reset() {
        pause()
        _positionUs.value = 0L
    }

    /**
     * 释放时钟资源。
     * 取消后台协程 Job，防止 ViewModel 销毁时引起的内存泄露。
     */
    fun release() {
        playJob?.cancel()
        playJob = null
    }
}
