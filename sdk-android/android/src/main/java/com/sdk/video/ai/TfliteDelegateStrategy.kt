package com.sdk.video.ai

import android.content.Context
import android.os.Build
import android.util.Log

/**
 * TFLite Delegate 自适应选择策略。
 *
 * ## 支持的 Delegate 类型
 * | 类型     | 说明                                    | 性能            |
 * |----------|----------------------------------------|-----------------|
 * | GPU      | OpenGL ES 3.1 / Vulkan — 最快          | 延迟最低         |
 * | NNAPI    | Android Neural Networks API (API 27+)  | 中等            |
 * | XNNPACK  | CPU SIMD — TFLite 内置，无需额外 .so    | 低端机最优       |
 * | CPU      | 纯 CPU 单线程 — 兜底                   | 最慢、兼容性最高  |
 *
 * ## 自动选择逻辑（AUTO 模式）
 * 1. GPU — 仅在 API 27+ 且 `HAS_TFLITE_GPU_DELEGATE` 编译时可用
 * 2. NNAPI — API 28+ 且非模拟器（模拟器 NNAPI 有已知 bug）
 * 3. XNNPACK — 默认 CPU 加速（TFLite 2.x 内置）
 *
 * ## 性能分档
 * - HIGH（旗舰/中高端）：GPU → NNAPI → XNNPACK
 * - BALANCED（中端）：NNAPI → XNNPACK
 * - POWER_SAVE（低端 / 后台导出）：XNNPACK → CPU
 *
 * ## 与 C++ 层的对接
 * `applyToNative(engineHandle)` 通过 JNI 调用 `TfliteInferenceEngine::setDelegateHint(int)`，
 * 让 C++ 在 `buildInterpreter()` 中优先尝试指定的 delegate。
 */
class TfliteDelegateStrategy(private val context: Context) {

    enum class DelegateType(val nativeCode: Int) {
        GPU    (0),
        NNAPI  (1),
        XNNPACK(2),
        CPU    (3)
    }

    enum class Preference {
        AUTO,        // 按设备能力自动选择
        GPU_ONLY,    // 强制 GPU（失败则不回退，返回 error）
        NNAPI_ONLY,  // 强制 NNAPI
        CPU_ONLY,    // 仅 CPU（省电 / 调试）
        POWER_SAVE   // 低功耗：XNNPACK 或 CPU
    }

    enum class PerformanceTier { HIGH, BALANCED, POWER_SAVE }

    companion object {
        private const val TAG = "TfliteDelegateStrategy"

        /** 是否在模拟器中（NNAPI 在模拟器上不可靠）。 */
        val isEmulator: Boolean by lazy {
            Build.FINGERPRINT.contains("generic") ||
            Build.FINGERPRINT.contains("unknown") ||
            Build.MODEL.contains("Emulator") ||
            Build.MODEL.contains("Android SDK")
        }
    }

    // ── 能力探测 ──────────────────────────────────────────────────────────────

    /** GPU delegate 是否可能可用（编译时 + 运行时粗检）。 */
    val isGpuLikelyAvailable: Boolean
        get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1 && !isEmulator

    /** NNAPI delegate 是否可能可用。 */
    val isNnapiLikelyAvailable: Boolean
        get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.P && !isEmulator

    // ── 分档推断 ──────────────────────────────────────────────────────────────

    /**
     * 根据 RAM 和 CPU 核心数推断设备性能分档。
     * 精确做法应使用 `ActivityManager.getMemoryInfo` 和 `Runtime.availableProcessors()`。
     */
    fun inferPerformanceTier(): PerformanceTier {
        val cores = Runtime.getRuntime().availableProcessors()
        val ramMb = run {
            val mi = android.app.ActivityManager.MemoryInfo()
            (context.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager)
                .getMemoryInfo(mi)
            mi.totalMem / (1024 * 1024)
        }
        return when {
            cores >= 8 && ramMb >= 6144 -> PerformanceTier.HIGH
            cores >= 4 && ramMb >= 3072 -> PerformanceTier.BALANCED
            else                         -> PerformanceTier.POWER_SAVE
        }
    }

    // ── 选择逻辑 ──────────────────────────────────────────────────────────────

    /**
     * 根据 [preference] 选择最佳 delegate。
     * @param preference  选择策略；[Preference.AUTO] 自动按分档选择
     * @return            推荐的 delegate 类型
     */
    fun select(preference: Preference = Preference.AUTO): DelegateType {
        val delegate = when (preference) {
            Preference.GPU_ONLY    -> if (isGpuLikelyAvailable) DelegateType.GPU else {
                Log.w(TAG, "GPU requested but not available; falling back to CPU")
                DelegateType.CPU
            }
            Preference.NNAPI_ONLY  -> if (isNnapiLikelyAvailable) DelegateType.NNAPI else {
                Log.w(TAG, "NNAPI requested but not available; falling back to XNNPACK")
                DelegateType.XNNPACK
            }
            Preference.CPU_ONLY    -> DelegateType.CPU
            Preference.POWER_SAVE  -> DelegateType.XNNPACK
            Preference.AUTO        -> autoSelect()
        }
        Log.d(TAG, "Delegate selected: $delegate (pref=$preference)")
        return delegate
    }

    private fun autoSelect(): DelegateType = when (inferPerformanceTier()) {
        PerformanceTier.HIGH     -> if (isGpuLikelyAvailable) DelegateType.GPU
                                    else if (isNnapiLikelyAvailable) DelegateType.NNAPI
                                    else DelegateType.XNNPACK
        PerformanceTier.BALANCED -> if (isNnapiLikelyAvailable) DelegateType.NNAPI
                                    else DelegateType.XNNPACK
        PerformanceTier.POWER_SAVE -> DelegateType.XNNPACK
    }

    /**
     * 将选定的 delegate 提示写入 C++ TfliteInferenceEngine。
     * 必须在加载模型（loadModel）之前调用。
     *
     * @param engineHandle  从 JNI 分配的 TfliteInferenceEngine 指针
     * @param delegate      由 [select] 返回的 delegate 类型
     */
    fun applyToNative(engineHandle: Long, delegate: DelegateType) {
        if (engineHandle == 0L) return
        try {
            nativeSetDelegateHint(engineHandle, delegate.nativeCode)
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "nativeSetDelegateHint not available: ${e.message}")
        }
    }

    // ── JNI 声明 ──────────────────────────────────────────────────────────────

    private external fun nativeSetDelegateHint(engineHandle: Long, delegateCode: Int)
}
