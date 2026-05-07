package com.sdk.video.export

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import androidx.core.app.NotificationCompat
import java.io.File

/**
 * BackgroundExportService — 后台视频导出前台服务
 *
 * 职责：
 *  - 以 Foreground Service 形式保持进程优先级（Android 要求长时间 CPU/GPU 任务必须持有前台通知）
 *  - 每 500ms 轮询 [TimelineExporter.getProgress] 并刷新通知进度条
 *  - 导出完成或取消时自动 stopSelf()
 *
 * 使用方式（由 [BackgroundExportManager] 管理，不需要直接使用本类）:
 *  1. Context.startForegroundService(Intent(ctx, BackgroundExportService::class.java))
 *  2. bindService → [ExportBinder.attachExporter]
 */
class BackgroundExportService : Service() {

    // -------------------------------------------------------------------------
    // Binder
    // -------------------------------------------------------------------------
    inner class ExportBinder : Binder() {
        /**
         * 将已配置并已启动的 [com.sdk.video.timeline.TimelineExporter] 注入本服务，
         * 服务将持续轮询其进度并更新通知栏。
         */
        fun attachExporter(exporter: com.sdk.video.timeline.TimelineExporter) {
            this@BackgroundExportService.attachExporter(exporter)
        }
    }

    private val binder = ExportBinder()
    private var exporter: com.sdk.video.timeline.TimelineExporter? = null
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var notificationManager: NotificationManager
    private var outputFileName: String = "video"

    // -------------------------------------------------------------------------
    // Service lifecycle
    // -------------------------------------------------------------------------
    override fun onCreate() {
        super.onCreate()
        notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_CANCEL) {
            exporter?.cancel()
            completeSelf()
            return START_NOT_STICKY
        }

        outputFileName = intent?.getStringExtra(EXTRA_OUTPUT_PATH)
            ?.let { File(it).name } ?: "video"

        startForeground(NOTIFICATION_ID, buildNotification(0f, outputFileName))
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent): IBinder = binder

    override fun onDestroy() {
        super.onDestroy()
        handler.removeCallbacksAndMessages(null)
    }

    // -------------------------------------------------------------------------
    // Internal
    // -------------------------------------------------------------------------
    private fun attachExporter(exporter: com.sdk.video.timeline.TimelineExporter) {
        this.exporter = exporter
        scheduleProgressPoll()
    }

    private fun scheduleProgressPoll() {
        handler.postDelayed(progressPoller, POLL_INTERVAL_MS)
    }

    private val progressPoller = object : Runnable {
        override fun run() {
            val exp = exporter ?: run { completeSelf(); return }

            val state    = exp.getState()      // 0=IDLE,1=STARTING,2=EXPORTING,3=COMPLETED,4=FAILED,5=CANCELED
            val progress = exp.getProgress()

            when (state) {
                STATE_COMPLETED -> {
                    notificationManager.notify(NOTIFICATION_ID,
                        buildCompletionNotification(outputFileName, success = true))
                    completeSelf()
                    return
                }
                STATE_FAILED, STATE_CANCELED -> {
                    notificationManager.notify(NOTIFICATION_ID,
                        buildCompletionNotification(outputFileName, success = false))
                    completeSelf()
                    return
                }
                else -> {
                    notificationManager.notify(NOTIFICATION_ID,
                        buildNotification(progress, outputFileName))
                }
            }
            handler.postDelayed(this, POLL_INTERVAL_MS)
        }
    }

    private fun completeSelf() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_DETACH)
        } else {
            @Suppress("DEPRECATION")
            stopForeground(false)
        }
        stopSelf()
    }

    // -------------------------------------------------------------------------
    // Notification helpers
    // -------------------------------------------------------------------------
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "视频导出",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "显示视频导出进度"
                setShowBadge(false)
            }
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(progress: Float, fileName: String): android.app.Notification {
        val progressInt = (progress * 100).toInt().coerceIn(0, 100)

        val cancelIntent = Intent(this, BackgroundExportService::class.java)
            .setAction(ACTION_CANCEL)
        val cancelPi = PendingIntent.getService(
            this, 0, cancelIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_upload)
            .setContentTitle("正在导出视频")
            .setContentText("$fileName  $progressInt%")
            .setProgress(100, progressInt, progressInt == 0)
            .addAction(android.R.drawable.ic_delete, "取消", cancelPi)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .build()
    }

    private fun buildCompletionNotification(fileName: String, success: Boolean): android.app.Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(
                if (success) android.R.drawable.stat_sys_upload_done
                else android.R.drawable.stat_notify_error
            )
            .setContentTitle(if (success) "导出完成" else "导出失败")
            .setContentText(fileName)
            .setAutoCancel(true)
            .build()
    }

    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------
    companion object {
        const val CHANNEL_ID         = "sdk_video_export_v1"
        const val NOTIFICATION_ID    = 0x5D4B_01
        const val EXTRA_OUTPUT_PATH  = "outputPath"
        const val ACTION_CANCEL      = "com.sdk.video.EXPORT_CANCEL"
        private const val POLL_INTERVAL_MS = 500L

        // State codes matching C++ TimelineExporter::State enum
        // IDLE=0, STARTING=1, EXPORTING=2, COMPLETED=3, CANCELED=4, FAILED=5
        private const val STATE_COMPLETED = 3
        private const val STATE_CANCELED  = 4
        private const val STATE_FAILED    = 5
    }
}
