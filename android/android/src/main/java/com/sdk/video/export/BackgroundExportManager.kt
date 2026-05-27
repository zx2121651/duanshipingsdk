@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.export

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Build
import android.os.IBinder
import androidx.lifecycle.LiveData
import androidx.work.*
import com.sdk.video.RenderEngine
import com.sdk.video.VideoExportConfig
import com.sdk.video.timeline.TimelineExporter
import com.sdk.video.timeline.TimelineManager
import java.util.UUID

/**
 * BackgroundExportManager — 后台视频导出公开 API
 *
 * 封装 [TimelineExporter] 与 [BackgroundExportService] 的整个生命周期：
 *  1. 启动 Foreground Service（显示系统通知栏进度）
 *  2. 绑定 Service 并注入 exporter 句柄
 *  3. 开始原生导出线程（C++ 侧使用独立 EGL context）
 *  4. 导出完成或取消后自动释放资源
 *
 * ## 典型用法
 * ```kotlin
 * val exportManager = BackgroundExportManager(context)
 * exportManager.startExport(
 *     timeline      = timelineManager,
 *     renderEngine  = renderEngine,
 *     config        = VideoExportConfig(outputPath = "/sdcard/output.mp4"),
 *     onProgress    = { progress -> progressBar.progress = (progress * 100).toInt() },
 *     onComplete    = { errorCode, msg ->
 *         if (errorCode == 0) showSuccess() else showError(msg)
 *         exportManager.release()
 *     }
 * )
 * // 取消：
 * exportManager.cancelExport()
 * ```
 */
class BackgroundExportManager(private val context: Context) {

    private val exporter = TimelineExporter()
    private var serviceBound = false

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as? BackgroundExportService.ExportBinder ?: return
            binder.attachExporter(exporter)
        }
        override fun onServiceDisconnected(name: ComponentName?) {
            serviceBound = false
        }
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /**
     * 配置并启动后台导出。
     *
     * @param timeline      已构建好的 [TimelineManager]（持有 native 时间线句柄）
     * @param renderEngine  已初始化的 [RenderEngine]（持有 native FilterEngine 句柄）
     * @param config        导出参数（分辨率、码率、输出路径等）
     * @param onProgress    进度回调 [0.0, 1.0]，在调用方线程触发
     * @param onComplete    完成回调 (errorCode: Int, message: String)，errorCode==0 表示成功
     */
    fun startExport(
        timeline:     TimelineManager,
        renderEngine: RenderEngine,
        config:       VideoExportConfig,
        onProgress:   (Float) -> Unit   = {},
        onComplete:   (Int, String) -> Unit = { _, _ -> }
    ) {
        // 1. Configure native exporter
        val cfgResult = exporter.configure(config)
        if (cfgResult != 0) {
            onComplete(cfgResult, "Exporter configuration failed (code=$cfgResult)")
            return
        }

        // 2. Start + bind Foreground Service
        val serviceIntent = Intent(context, BackgroundExportService::class.java).apply {
            putExtra(BackgroundExportService.EXTRA_OUTPUT_PATH, config.outputPath)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(serviceIntent)
        } else {
            context.startService(serviceIntent)
        }
        context.bindService(serviceIntent, serviceConnection, Context.BIND_AUTO_CREATE)
        serviceBound = true

        // 3. Launch native export (runs on its own C++ thread + EGL context)
        exporter.exportAsync(
            timeline     = timeline,
            renderEngine = renderEngine,
            onProgress   = { progress -> onProgress(progress) },
            onComplete   = { errorCode, message ->
                onComplete(errorCode, message)
                // Service will stop itself via polling; unbind here
                safeUnbind()
            }
        )
    }

    /**
     * 取消正在进行的导出。
     * 取消是异步的，[onComplete] 将以 ERR_EXPORTER_CANCELLED 码被回调。
     */
    fun cancelExport() {
        exporter.cancel()
        workRequestId?.let { WorkManager.getInstance(context).cancelWorkById(it) }
    }

    // -------------------------------------------------------------------------
    // WorkManager overload — survives process death, respects Android 12+ limits
    // -------------------------------------------------------------------------

    private var workRequestId: UUID? = null

    /**
     * 通过 WorkManager 提交后台导出任务（推荐用于 Android 12+）。
     *
     * 与 [startExport] 的区别：
     * - 进程被强杀后 WorkManager 自动重新调度 Worker。
     * - 进度通过返回的 [LiveData]<[WorkInfo]> 观察（`workInfo.progress`）。
     * - 不需要调用方管理 Service 生命周期。
     *
     * @param timeline    已构建的 [TimelineManager] 实例
     * @param renderEngine 已初始化的 [RenderEngine] 实例
     * @param config      导出参数（outputPath / width / height / fps / videoBitrate）
     * @return            [LiveData]<[WorkInfo]>，可在 ViewModel/Fragment 中 observe
     */
    fun startExportViaWorkManager(
        timeline:     TimelineManager,
        renderEngine: RenderEngine,
        config:       VideoExportConfig
    ): LiveData<WorkInfo> {
        val liveData = BackgroundExportWorker.enqueue(context, timeline, renderEngine, config)
        workRequestId = null  // ID is managed inside BackgroundExportWorker.enqueue
        return liveData
    }

    /**
     * 释放资源（建议在 onComplete 回调中调用，或在 Activity.onDestroy 中调用）。
     */
    fun release() {
        safeUnbind()
        exporter.release()
    }

    // -------------------------------------------------------------------------
    // Internal
    // -------------------------------------------------------------------------
    private fun safeUnbind() {
        if (serviceBound) {
            try { context.unbindService(serviceConnection) } catch (_: Exception) {}
            serviceBound = false
        }
    }
}
