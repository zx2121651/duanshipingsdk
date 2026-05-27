package com.sdk.video.ai

import android.content.Context
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.io.File

/**
 * SubtitleController
 *
 * 自动字幕全链路封装。
 *
 * 架构：
 *   - 语音识别由可注入的 [AsrEngine] 接口负责（默认 StubAsrEngine，不崩溃）
 *   - 平台实现（VoskEngine / WhisperEngine / AndroidSpeechEngine）通过 [registerEngine] 注入
 *   - 输出 [SubtitleSegment] 列表，可直接与时间线 SubtitleClip 对接
 *
 * 用法：
 *   val ctrl = SubtitleController(context)
 *   SubtitleController.registerEngine { VoskAsrEngine(context) }   // 可选，注册离线引擎
 *
 *   ctrl.recognize("/data/audio.wav", durationNs = 30_000_000_000L)
 *       .collect { state ->
 *           when (state) {
 *               is SubtitleState.Progress -> updateProgressBar(state.percent)
 *               is SubtitleState.Done     -> addSubtitlesToTimeline(state.segments)
 *               is SubtitleState.Error    -> showError(state.message)
 *           }
 *       }
 */
class SubtitleController(
    private val context: Context,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
) {

    // ---------------------------------------------------------------------------
    // Public data types
    // ---------------------------------------------------------------------------

    data class SubtitleSegment(
        val id: String,
        val text: String,
        val startNs: Long,
        val endNs: Long,
        val confidence: Float = 1f
    )

    sealed class SubtitleState {
        data class Progress(val percent: Float) : SubtitleState()
        data class Done(val segments: List<SubtitleSegment>) : SubtitleState()
        data class Error(val message: String) : SubtitleState()
    }

    // ---------------------------------------------------------------------------
    // AsrEngine interface — platform implementations injected via registerEngine()
    // ---------------------------------------------------------------------------

    interface AsrEngine {
        fun recognize(audioPath: String, durationNs: Long): List<SubtitleSegment>
        fun cancel() {}
        fun release() {}
    }

    // ---------------------------------------------------------------------------
    // Factory registration (mirrors C++ AutoSubtitleEngine::registerFactory)
    // ---------------------------------------------------------------------------

    companion object {
        private const val TAG = "SubtitleController"
        @Volatile private var engineFactory: (() -> AsrEngine)? = null

        fun registerEngine(factory: () -> AsrEngine) {
            engineFactory = factory
            Log.i(TAG, "AsrEngine factory registered")
        }
    }

    // ---------------------------------------------------------------------------
    // Recognition
    // ---------------------------------------------------------------------------

    private var activeEngine: AsrEngine? = null
    private var language: String = "zh"
    private var modelPath: String? = null

    fun setLanguage(lang: String) { language = lang }
    fun setModelPath(path: String) { modelPath = path }

    /**
     * 识别 PCM/WAV 音频文件，以 Flow 形式发射进度和最终结果。
     */
    fun recognize(audioPath: String, durationNs: Long): Flow<SubtitleState> = flow {
        emit(SubtitleState.Progress(0f))
        val file = File(audioPath)
        if (!file.exists()) {
            emit(SubtitleState.Error("Audio file not found: $audioPath"))
            return@flow
        }
        val engine = buildEngine()
        activeEngine = engine
        try {
            emit(SubtitleState.Progress(0.1f))
            val segments = withContext(Dispatchers.IO) {
                engine.recognize(audioPath, durationNs)
            }
            emit(SubtitleState.Progress(1f))
            emit(SubtitleState.Done(segments))
        } catch (e: Exception) {
            Log.e(TAG, "Recognition failed", e)
            emit(SubtitleState.Error(e.message ?: "Unknown error"))
        } finally {
            activeEngine = null
        }
    }.flowOn(Dispatchers.IO)

    /** 取消正在进行的识别。 */
    fun cancel() { activeEngine?.cancel() }

    /** 释放资源。 */
    fun release() {
        activeEngine?.release()
        activeEngine = null
        scope.cancel()
    }

    // ---------------------------------------------------------------------------
    // Text style helpers (供 UI 层快速构建字幕样式)
    // ---------------------------------------------------------------------------

    data class SubtitleStyle(
        val fontSizeSp: Int = 18,
        val textColor: Int = 0xFFFFFFFF.toInt(),
        val bgColor: Int = 0x80000000.toInt(),
        val bold: Boolean = false,
        val positionY: Float = 0.88f,   // 归一化，0=顶 1=底
        val alignment: Int = 1          // 0=left 1=center 2=right
    )

    var defaultStyle: SubtitleStyle = SubtitleStyle()

    // ---------------------------------------------------------------------------
    // Private helpers
    // ---------------------------------------------------------------------------

    private fun buildEngine(): AsrEngine {
        val factory = engineFactory
        return if (factory != null) {
            try { factory() } catch (e: Exception) {
                Log.w(TAG, "Engine factory threw: ${e.message}, falling back to stub")
                StubAsrEngine()
            }
        } else {
            Log.w(TAG, "No AsrEngine registered — using stub (returns empty segments)")
            StubAsrEngine()
        }
    }

    // ---------------------------------------------------------------------------
    // Stub engine — safe no-op when no real ASR is registered
    // ---------------------------------------------------------------------------

    private class StubAsrEngine : AsrEngine {
        override fun recognize(audioPath: String, durationNs: Long): List<SubtitleSegment> = emptyList()
    }

}
