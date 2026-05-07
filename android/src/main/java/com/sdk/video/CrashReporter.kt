package com.sdk.video

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean

/**
 * SDK 崩溃 / ANR 观测器。
 *
 * ## 功能
 * 1. **Crash 捕获** — 通过 `Thread.setDefaultUncaughtExceptionHandler` 捕获未处理异常，
 *    将堆栈写入 `filesDir/crash_reports/<timestamp>.txt`，然后转交原始 handler。
 * 2. **ANR 检测** — 主线程 Watchdog：每隔 [anrThresholdMs] 毫秒 ping 主线程；
 *    若 ping 未在时限内返回，则视为 ANR 并写入报告（不强制终止进程）。
 * 3. **历史报告** — `getReports()` 返回最近 N 条报告文件列表；
 *    `clearReports()` 清空全部。
 * 4. **自定义 sink** — `addSink(sink)` 注册外部上报接口（如 Firebase Crashlytics），
 *    每条报告同时回调所有已注册 sink。
 *
 * ## 使用
 * ```kotlin
 * CrashReporter.init(applicationContext, anrThresholdMs = 5000L)
 * CrashReporter.addSink { report -> Crashlytics.log(report.summary) }
 * ```
 *
 * ## 注意
 * - `init()` 应在 `Application.onCreate()` 中调用，且只调用一次。
 * - ANR Watchdog 在独立后台线程中运行，进程退出后自动停止。
 * - 本模块不包含网络上报逻辑，由外部 sink 负责。
 */
object CrashReporter {

    data class CrashReport(
        val type: Type,
        val summary: String,
        val stackTrace: String,
        val threadName: String,
        val timestampMs: Long = System.currentTimeMillis(),
        val file: File? = null
    ) {
        enum class Type { CRASH, ANR, NATIVE }
    }

    fun interface Sink {
        fun onReport(report: CrashReport)
    }

    private const val TAG = "CrashReporter"
    private const val REPORT_DIR = "crash_reports"
    private const val MAX_REPORTS = 20
    private val DATE_FMT = SimpleDateFormat("yyyyMMdd_HHmmss_SSS", Locale.US)

    private val initialized = AtomicBoolean(false)
    private val sinks = mutableListOf<Sink>()
    private lateinit var reportsDir: File

    @Volatile private var anrWatchdog: AnrWatchdog? = null

    // ── 初始化 ────────────────────────────────────────────────────────────────

    /**
     * 初始化崩溃观测器。
     * @param context          Application Context
     * @param anrThresholdMs   ANR 判定阈值（ms）；0 = 禁用 ANR 检测
     */
    fun init(context: Context, anrThresholdMs: Long = 5_000L) {
        if (!initialized.compareAndSet(false, true)) {
            Log.w(TAG, "CrashReporter.init() called more than once — ignored")
            return
        }
        reportsDir = File(context.filesDir, REPORT_DIR).also { it.mkdirs() }

        installUncaughtExceptionHandler()

        if (anrThresholdMs > 0L) {
            anrWatchdog = AnrWatchdog(anrThresholdMs).also { it.start() }
        }

        Log.i(TAG, "CrashReporter initialized (anrThreshold=${anrThresholdMs}ms)")
    }

    // ── Sink 注册 ─────────────────────────────────────────────────────────────

    fun addSink(sink: Sink) = synchronized(sinks) { sinks += sink }
    fun removeSink(sink: Sink) = synchronized(sinks) { sinks -= sink }

    // ── 报告管理 ──────────────────────────────────────────────────────────────

    /** 最近 [max] 条报告文件（按时间降序）。 */
    fun getReports(max: Int = MAX_REPORTS): List<File> =
        reportsDir.listFiles()
            ?.sortedByDescending { it.lastModified() }
            ?.take(max)
            ?: emptyList()

    /** 删除所有崩溃报告文件。 */
    fun clearReports() {
        reportsDir.listFiles()?.forEach { it.delete() }
    }

    // ── 内部实现 ──────────────────────────────────────────────────────────────

    private fun installUncaughtExceptionHandler() {
        val originalHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                val report = buildReport(CrashReport.Type.CRASH, thread, throwable)
                writeReport(report)
                dispatchToSinks(report)
            } catch (e: Exception) {
                Log.e(TAG, "Error writing crash report", e)
            } finally {
                originalHandler?.uncaughtException(thread, throwable)
            }
        }
    }

    private fun buildReport(
        type: CrashReport.Type,
        thread: Thread,
        throwable: Throwable
    ): CrashReport {
        val sw = StringWriter()
        throwable.printStackTrace(PrintWriter(sw))
        return CrashReport(
            type        = type,
            summary     = "${throwable::class.java.simpleName}: ${throwable.message}",
            stackTrace  = sw.toString(),
            threadName  = thread.name
        )
    }

    private fun writeReport(report: CrashReport): File? {
        if (!::reportsDir.isInitialized) return null
        return try {
            // GC old reports if over limit
            pruneOldReports()
            val ts   = DATE_FMT.format(Date(report.timestampMs))
            val name = "${report.type.name.lowercase()}_$ts.txt"
            val file = File(reportsDir, name)
            file.writeText(buildString {
                appendLine("=== SVDK ${report.type} REPORT ===")
                appendLine("Time:    $ts")
                appendLine("Thread:  ${report.threadName}")
                appendLine("Summary: ${report.summary}")
                appendLine()
                appendLine(report.stackTrace)
            })
            Log.i(TAG, "Report written: ${file.name}")
            file
        } catch (e: Exception) {
            Log.e(TAG, "writeReport failed", e)
            null
        }
    }

    private fun pruneOldReports() {
        val files = reportsDir.listFiles()?.sortedBy { it.lastModified() } ?: return
        if (files.size >= MAX_REPORTS) {
            files.take(files.size - MAX_REPORTS + 1).forEach { it.delete() }
        }
    }

    private fun dispatchToSinks(report: CrashReport) {
        synchronized(sinks) { sinks.toList() }.forEach { sink ->
            try { sink.onReport(report) }
            catch (e: Exception) { Log.e(TAG, "Sink threw", e) }
        }
    }

    // ── ANR Watchdog ──────────────────────────────────────────────────────────

    private inner class AnrWatchdog(private val thresholdMs: Long) : Thread("svdk-anr-watchdog") {
        @Volatile private var running = true
        @Volatile private var tick = 0L
        @Volatile private var tock = 0L

        private val mainHandler = Handler(Looper.getMainLooper())

        init { isDaemon = true; priority = MIN_PRIORITY }

        override fun run() {
            while (running) {
                tick++
                val expected = tick
                mainHandler.post { tock = tick }

                try { sleep(thresholdMs) } catch (_: InterruptedException) { break }

                if (running && tock < expected) {
                    reportAnr()
                }
            }
        }

        private fun reportAnr() {
            val mainThread = Looper.getMainLooper().thread
            val trace = buildString {
                mainThread.stackTrace.forEach { appendLine("\tat $it") }
            }
            val report = CrashReport(
                type        = CrashReport.Type.ANR,
                summary     = "Main thread blocked for >${thresholdMs}ms",
                stackTrace  = "Main thread stack:\n$trace",
                threadName  = mainThread.name
            )
            Log.w(TAG, "ANR detected: ${report.summary}")
            writeReport(report)
            dispatchToSinks(report)
        }

        fun halt() { running = false; interrupt() }
    }
}
