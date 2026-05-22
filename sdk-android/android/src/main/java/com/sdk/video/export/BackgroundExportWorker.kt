@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.export

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.pm.ServiceInfo
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.work.*
import com.sdk.video.RenderEngine
import com.sdk.video.VideoExportConfig
import com.sdk.video.timeline.TimelineExporter
import com.sdk.video.timeline.TimelineManager
import kotlinx.coroutines.suspendCancellableCoroutine
import java.io.File
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import kotlin.coroutines.resume

/**
 * BackgroundExportWorker — WorkManager 后台导出 Worker（Android 12+ 兼容）
 *
 * ## 设计要点
 * - 继承 `CoroutineWorker`，进程被杀后由 WorkManager 自动重排调度。
 * - JNI 对象（[TimelineManager] / [RenderEngine]）不能序列化进 [Data]；
 *   通过 [companion object] 持有的静态 [pendingJobs] 表（in-process 传递）绕过此限制。
 * - 使用 `setForegroundAsync` 显示系统通知栏进度（API 26+）。
 * - 进度通过 `setProgressAsync(workDataOf(KEY_PROGRESS to Float))` 上报。
 * - 取消时调用 [TimelineExporter.cancel]，清理写到一半的临时文件。
 *
 * ## 典型用法
 * ```kotlin
 * val liveData = BackgroundExportWorker.enqueue(
 *     context      = ctx,
 *     timeline     = timelineManager,
 *     renderEngine = renderEngine,
 *     config       = VideoExportConfig(outputPath = "/sdcard/out.mp4")
 * )
 * liveData.observe(lifecycleOwner) { info ->
 *     val progress = info.progress.getFloat(KEY_PROGRESS, 0f)
 * }
 * ```
 */
class BackgroundExportWorker(
    context: Context,
    params:  WorkerParameters
) : CoroutineWorker(context, params) {

    companion object {
        const val KEY_JOB_ID        = "jobId"
        const val KEY_OUTPUT_PATH   = "outputPath"
        const val KEY_PROGRESS      = "progress"
        const val KEY_ERROR_CODE    = "errorCode"
        const val KEY_ERROR_MSG     = "errorMsg"

        private const val CHANNEL_ID      = "sdk_video_export_worker_v1"
        private const val NOTIFICATION_ID = 0x5D4B_02

        private data class ExportJob(
            val timeline:     TimelineManager,
            val renderEngine: RenderEngine,
            val config:       VideoExportConfig
        )

        private val pendingJobs = ConcurrentHashMap<String, ExportJob>()

        /**
         * 提交 WorkManager 导出任务。返回 [WorkInfo] [LiveData] 供调用方观察进度。
         *
         * @param context       应用 Context
         * @param timeline      已构建的 [TimelineManager] 实例
         * @param renderEngine  已初始化的 [RenderEngine] 实例
         * @param config        导出配置
         */
        fun enqueue(
            context:      Context,
            timeline:     TimelineManager,
            renderEngine: RenderEngine,
            config:       VideoExportConfig
        ): androidx.lifecycle.LiveData<WorkInfo> {
            val jobId = UUID.randomUUID().toString()
            pendingJobs[jobId] = ExportJob(timeline, renderEngine, config)

            val request = OneTimeWorkRequestBuilder<BackgroundExportWorker>()
                .setInputData(workDataOf(
                    KEY_JOB_ID      to jobId,
                    KEY_OUTPUT_PATH to config.outputPath
                ))
                .setExpedited(OutOfQuotaPolicy.RUN_AS_NON_EXPEDITED_WORK_REQUEST)
                .addTag("sdk_video_export")
                .build()

            WorkManager.getInstance(context).enqueueUniqueWork(
                "sdk_video_export_${config.outputPath.hashCode()}",
                ExistingWorkPolicy.REPLACE,
                request
            )
            return WorkManager.getInstance(context).getWorkInfoByIdLiveData(request.id)
        }
    }

    private val exporter = TimelineExporter()

    override suspend fun doWork(): Result {
        val jobId      = inputData.getString(KEY_JOB_ID)
            ?: return Result.failure(workDataOf(KEY_ERROR_MSG to "Missing jobId"))
        val outputPath = inputData.getString(KEY_OUTPUT_PATH)
            ?: return Result.failure(workDataOf(KEY_ERROR_MSG to "Missing outputPath"))

        val job = pendingJobs.remove(jobId)
            ?: return Result.failure(workDataOf(KEY_ERROR_MSG to "Job $jobId not found (process restarted?)"))

        setForeground(createForegroundInfo(0f))

        val cfgCode = exporter.configure(job.config)
        if (cfgCode != 0) {
            return Result.failure(workDataOf(
                KEY_ERROR_CODE to cfgCode,
                KEY_ERROR_MSG  to "Exporter configuration failed (code=$cfgCode)"
            ))
        }

        return try {
            val (errorCode, errorMsg) = runExportSuspend(job)
            if (isStopped || errorCode == -5006) {
                cleanupPartialFile(outputPath)
                Result.failure(workDataOf(KEY_ERROR_CODE to -5006, KEY_ERROR_MSG to "Cancelled"))
            } else if (errorCode == 0) {
                Result.success(workDataOf(KEY_PROGRESS to 1f, KEY_ERROR_CODE to 0))
            } else {
                cleanupPartialFile(outputPath)
                Result.failure(workDataOf(KEY_ERROR_CODE to errorCode, KEY_ERROR_MSG to errorMsg))
            }
        } finally {
            exporter.release()
        }
    }

    private suspend fun runExportSuspend(job: ExportJob): Pair<Int, String> =
        suspendCancellableCoroutine { cont ->
            exporter.exportAsync(
                timeline     = job.timeline,
                renderEngine = job.renderEngine,
                onProgress   = { progress ->
                    setProgressAsync(workDataOf(KEY_PROGRESS to progress))
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        setForegroundAsync(createForegroundInfo(progress))
                    }
                },
                onComplete   = { errorCode, message ->
                    cont.resume(errorCode to message)
                }
            )
            cont.invokeOnCancellation { exporter.cancel() }
        }

    private fun createForegroundInfo(progress: Float): ForegroundInfo {
        ensureNotificationChannel()
        val pct = (progress * 100).toInt()
        val notification = NotificationCompat.Builder(applicationContext, CHANNEL_ID)
            .setContentTitle("视频导出中")
            .setContentText("$pct%")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setProgress(100, pct, pct == 0)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .build()

        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ForegroundInfo(NOTIFICATION_ID, notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        } else {
            ForegroundInfo(NOTIFICATION_ID, notification)
        }
    }

    private fun ensureNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = applicationContext.getSystemService(Context.NOTIFICATION_SERVICE)
                    as NotificationManager
            if (nm.getNotificationChannel(CHANNEL_ID) == null) {
                nm.createNotificationChannel(
                    NotificationChannel(CHANNEL_ID, "视频导出",
                        NotificationManager.IMPORTANCE_LOW
                    ).apply { description = "SDK 后台导出进度" }
                )
            }
        }
    }

    private fun cleanupPartialFile(path: String) {
        try { File(path).takeIf { it.exists() }?.delete() } catch (_: Exception) {}
    }
}
