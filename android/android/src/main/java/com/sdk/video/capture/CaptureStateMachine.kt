package com.sdk.video.capture

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

// ---------------------------------------------------------------------------
// States
// ---------------------------------------------------------------------------
sealed class CaptureState {
    /** 空闲，引擎未启动 */
    object Idle : CaptureState()

    /** 预览中，相机帧可见但未开始录制 */
    object Previewing : CaptureState()

    /**
     * 录制中
     * @param segmentIndex  当前分段的序号（从 0 起）
     * @param segmentStartMs  本分段开始时刻（System.currentTimeMillis）
     */
    data class Recording(
        val segmentIndex: Int,
        val segmentStartMs: Long
    ) : CaptureState()

    /**
     * 分段暂停：按下录制键后松开，至少已有一段
     * @param segmentCount   已录制的分段数
     * @param totalDurationMs 所有已完成分段的累计时长
     */
    data class SegmentPaused(
        val segmentCount: Int,
        val totalDurationMs: Long
    ) : CaptureState()

    /** 正在合并所有分段 */
    object Finalizing : CaptureState()

    /** 完成，outputPath 为最终合并文件路径 */
    data class Done(val outputPath: String) : CaptureState()

    /** 错误态 */
    data class Error(val cause: Throwable) : CaptureState()
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
sealed class CaptureEvent {
    object StartPreview : CaptureEvent()
    object PressRecord : CaptureEvent()
    object ReleaseRecord : CaptureEvent()
    object DeleteLastSegment : CaptureEvent()
    object Finish : CaptureEvent()
    object Cancel : CaptureEvent()
    data class SegmentFinalized(val totalDurationMs: Long) : CaptureEvent()
    data class FinalizeComplete(val outputPath: String) : CaptureEvent()
    data class Fail(val cause: Throwable) : CaptureEvent()
}

// ---------------------------------------------------------------------------
// State Machine
// ---------------------------------------------------------------------------
/**
 * 实时拍摄状态机。
 *
 * 合法迁移图：
 *
 *   Idle ──StartPreview──► Previewing
 *   Previewing ──PressRecord──► Recording(0)
 *   Recording ──ReleaseRecord──► SegmentPaused(n, totalMs)
 *   SegmentPaused ──PressRecord──► Recording(n)
 *   SegmentPaused ──DeleteLastSegment──► SegmentPaused(n-1) | Previewing (if n==1)
 *   SegmentPaused ──Finish──► Finalizing
 *   Finalizing ──FinalizeComplete──► Done
 *   任何状态 ──Cancel──► Previewing
 *   任何状态 ──Fail──► Error
 *
 * 所有 dispatch() 调用均为线程安全（StateFlow 内部原子更新）。
 */
class CaptureStateMachine {

    private val _state = MutableStateFlow<CaptureState>(CaptureState.Idle)

    /** 当前状态流，供 Compose / LiveData 观察。 */
    val state: StateFlow<CaptureState> = _state.asStateFlow()

    /** 当前状态快照（无锁读）。 */
    val current: CaptureState get() = _state.value

    /**
     * 触发一个事件并尝试转移状态。
     * 若当前状态对该事件没有定义合法转移，则静默忽略（不抛异常）。
     */
    fun dispatch(event: CaptureEvent) {
        val next = transition(current, event)
        if (next != null) _state.value = next
    }

    private fun transition(state: CaptureState, event: CaptureEvent): CaptureState? = when {

        // ── 启动预览 ──────────────────────────────────────
        state is CaptureState.Idle && event is CaptureEvent.StartPreview ->
            CaptureState.Previewing

        // ── 开始录制（首段 / 续段）────────────────────────
        state is CaptureState.Previewing && event is CaptureEvent.PressRecord ->
            CaptureState.Recording(
                segmentIndex = 0,
                segmentStartMs = System.currentTimeMillis()
            )

        state is CaptureState.SegmentPaused && event is CaptureEvent.PressRecord ->
            CaptureState.Recording(
                segmentIndex = state.segmentCount,
                segmentStartMs = System.currentTimeMillis()
            )

        // ── 松开录制键 → 分段完成 ─────────────────────────
        state is CaptureState.Recording && event is CaptureEvent.ReleaseRecord ->
            CaptureState.SegmentPaused(
                segmentCount = state.segmentIndex + 1,
                totalDurationMs = 0L  // 由 SegmentRecorder 通过 SegmentFinalized 事件更新
            )

        // ── 更新累计时长（由 SegmentRecorder 触发）─────────
        state is CaptureState.SegmentPaused && event is CaptureEvent.SegmentFinalized ->
            state.copy(totalDurationMs = event.totalDurationMs)

        // ── 删除最后一段 ──────────────────────────────────
        state is CaptureState.SegmentPaused && event is CaptureEvent.DeleteLastSegment ->
            if (state.segmentCount <= 1) CaptureState.Previewing
            else state.copy(segmentCount = state.segmentCount - 1)

        // ── 完成 / 合并 ───────────────────────────────────
        state is CaptureState.SegmentPaused && event is CaptureEvent.Finish ->
            CaptureState.Finalizing

        state is CaptureState.Finalizing && event is CaptureEvent.FinalizeComplete ->
            CaptureState.Done(outputPath = event.outputPath)

        // ── 取消（回到预览）──────────────────────────────
        event is CaptureEvent.Cancel ->
            CaptureState.Previewing

        // ── 错误 ─────────────────────────────────────────
        event is CaptureEvent.Fail ->
            CaptureState.Error(cause = event.cause)

        else -> null  // 无效转移，静默忽略
    }
}
