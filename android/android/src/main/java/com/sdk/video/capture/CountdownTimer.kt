package com.sdk.video.capture

import android.os.CountDownTimer
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * CountdownTimer
 *
 * 定时拍摄倒计时（P2 补齐）。
 *
 * 功能：
 *   - 支持 0s（立即拍）/ 3s / 5s / 10s 倒计时
 *   - StateFlow 暴露剩余秒数，Compose UI 直接 collectAsState()
 *   - 倒计时结束后触发 onComplete 回调（启动拍照/录像）
 *   - 可随时取消
 *
 * 用法（Compose ViewModel）：
 *   val countdown = CountdownTimer()
 *   countdown.start(delaySeconds = 3) {
 *       photoCapture.capture { ... }
 *   }
 *   // UI
 *   val remaining by countdown.remainingSeconds.collectAsState()
 *   if (remaining > 0) CountdownOverlay(remaining)
 */
class CountdownTimer {

    companion object {
        private const val TAG = "CountdownTimer"

        val PRESETS = listOf(0, 3, 5, 10)
    }

    // ── 状态 ───────────────────────────────────────────────────────────────

    private val _remainingSeconds = MutableStateFlow(0)
    /** 剩余秒数（0 = 空闲或已完成）。 */
    val remainingSeconds: StateFlow<Int> = _remainingSeconds.asStateFlow()

    private val _isRunning = MutableStateFlow(false)
    val isRunning: StateFlow<Boolean> = _isRunning.asStateFlow()

    private var timer: CountDownTimer? = null

    // ── API ────────────────────────────────────────────────────────────────

    /**
     * 启动倒计时。
     * @param delaySeconds  延迟秒数（0 表示立即触发）
     * @param onComplete    倒计时结束时调用（主线程）
     */
    fun start(delaySeconds: Int, onComplete: () -> Unit) {
        cancel()
        if (delaySeconds <= 0) {
            Log.d(TAG, "Immediate trigger (0s delay)")
            onComplete()
            return
        }

        _remainingSeconds.value = delaySeconds
        _isRunning.value = true
        Log.d(TAG, "Countdown started: ${delaySeconds}s")

        timer = object : CountDownTimer(delaySeconds * 1000L, 1000L) {
            override fun onTick(millisUntilFinished: Long) {
                val secs = ((millisUntilFinished + 999) / 1000).toInt()
                _remainingSeconds.value = secs
                Log.d(TAG, "Countdown: $secs")
            }
            override fun onFinish() {
                _remainingSeconds.value = 0
                _isRunning.value = false
                Log.d(TAG, "Countdown finished — firing onComplete")
                onComplete()
            }
        }.start()
    }

    /**
     * 取消正在进行的倒计时。
     */
    fun cancel() {
        timer?.cancel()
        timer = null
        _remainingSeconds.value = 0
        _isRunning.value = false
    }

    /**
     * 循环到下一个预设延迟（0→3→5→10→0）。
     * @param current  当前延迟秒数
     */
    fun cyclePreset(current: Int): Int {
        val idx = PRESETS.indexOf(current)
        return PRESETS[(idx + 1) % PRESETS.size]
    }
}
