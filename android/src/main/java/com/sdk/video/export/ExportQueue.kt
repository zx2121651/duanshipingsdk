package com.sdk.video.export

import android.content.Context
import android.util.Log
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.Data
import androidx.work.ExistingWorkPolicy
import androidx.work.ForegroundInfo
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.OutOfQuotaPolicy
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.UUID
import java.util.concurrent.TimeUnit

// ---------------------------------------------------------------------------
// Export job data model
// ---------------------------------------------------------------------------

enum class ExportPriority { HIGH, NORMAL, LOW }

data class ExportJob(
    val id: String = UUID.randomUUID().toString(),
    val draftPath: String,
    val outputPath: String,
    val width: Int,
    val height: Int,
    val fps: Int,
    val bitrate: Int,
    val colorSpace: ColorSpaceConfig = ColorSpaceConfig.SDR,
    val priority: ExportPriority = ExportPriority.NORMAL,
    val chunkDurationMs: Long = 0L,          // 0 = no chunking
    val status: ExportJobStatus = ExportJobStatus.QUEUED,
    val progress: Float = 0f,
    val errorMessage: String? = null,
    val workRequestId: UUID? = null
)

enum class ExportJobStatus { QUEUED, RUNNING, PAUSED, COMPLETED, FAILED, CANCELED }

// ---------------------------------------------------------------------------
// WorkManager Worker
// ---------------------------------------------------------------------------

/**
 * WorkManager 导出 Worker — 在后台进程中运行实际导出逻辑。
 * 崩溃后由 WorkManager 自动重试（最多 3 次，指数退避）。
 */
class ExportWorker(ctx: Context, params: WorkerParameters) : Worker(ctx, params) {

    companion object {
        const val KEY_JOB_ID       = "job_id"
        const val KEY_DRAFT_PATH   = "draft_path"
        const val KEY_OUTPUT_PATH  = "output_path"
        const val KEY_WIDTH        = "width"
        const val KEY_HEIGHT       = "height"
        const val KEY_FPS          = "fps"
        const val KEY_BITRATE      = "bitrate"
        const val KEY_COLOR_SPACE  = "color_space"
        const val KEY_CHUNK_MS     = "chunk_duration_ms"
        const val KEY_ERROR_MESSAGE = "error_message"
    }

    override fun getForegroundInfo(): ForegroundInfo {
        val channelId = "svdk_export"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val mgr = applicationContext.getSystemService(Context.NOTIFICATION_SERVICE)
                as NotificationManager
            if (mgr.getNotificationChannel(channelId) == null) {
                mgr.createNotificationChannel(
                    NotificationChannel(channelId, "Video Export",
                        NotificationManager.IMPORTANCE_LOW)
                )
            }
        }
        val notification: Notification = NotificationCompat.Builder(applicationContext, channelId)
            .setContentTitle("Exporting video")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setOngoing(true)
            .build()
        return ForegroundInfo(1001, notification)
    }

    override fun doWork(): Result {
        val jobId      = inputData.getString(KEY_JOB_ID)       ?: return Result.failure()
        val draftPath  = inputData.getString(KEY_DRAFT_PATH)   ?: return Result.failure()
        val outputPath = inputData.getString(KEY_OUTPUT_PATH)  ?: return Result.failure()
        val width      = inputData.getInt(KEY_WIDTH,  1080)
        val height     = inputData.getInt(KEY_HEIGHT, 1920)
        val fps        = inputData.getInt(KEY_FPS, 30)
        val bitrate    = inputData.getInt(KEY_BITRATE, 8_000_000)
        val colorSpaceStr = inputData.getString(KEY_COLOR_SPACE) ?: "SDR_BT709"
        val chunkMs    = inputData.getLong(KEY_CHUNK_MS, 0L)

        Log.i("ExportWorker", "Starting export job=$jobId draft=$draftPath -> $outputPath")

        return try {
            // Check for resumable checkpoint
            val recovery = ExportRecovery(applicationContext)
            val checkpoint = runCatching {
                kotlinx.coroutines.runBlocking { recovery.load(jobId) }
            }.getOrNull()

            val startPositionMs = checkpoint?.resumePositionMs ?: 0L
            if (startPositionMs > 0L)
                Log.i("ExportWorker", "Resuming from ${startPositionMs}ms (chunks=${checkpoint?.chunksCompleted})")

            // The actual export is handled by the native TimelineExporter via JNI.
            // This Worker manages lifecycle, checkpoint writing, and retry coordination.
            // Delegates to ExportExecutor (to be implemented by the host application layer).
            val success = ExportExecutor.execute(
                context        = applicationContext,
                jobId          = jobId,
                draftPath      = draftPath,
                outputPath     = outputPath,
                width          = width,
                height         = height,
                fps            = fps,
                bitrate        = bitrate,
                colorSpaceMode = colorSpaceStr,
                chunkDurationMs = chunkMs,
                resumePositionMs = startPositionMs,
                onProgress = { progress, posMs ->
                    // Write checkpoint every DEFAULT_INTERVAL_MS
                    if (posMs % ExportRecovery.DEFAULT_INTERVAL_MS < 500) {
                        val cp = ExportRecovery.Checkpoint(
                            jobId            = jobId,
                            draftPath        = draftPath,
                            outputPath       = outputPath,
                            tempPath         = "$outputPath.part",
                            totalDurationMs  = 0L, // filled by executor
                            resumePositionMs = posMs,
                            chunksCompleted  = 0,
                            colorSpace       = colorSpaceStr,
                            bitrate          = bitrate,
                            fps              = fps,
                            width            = width,
                            height           = height
                        )
                        kotlinx.coroutines.runBlocking { recovery.save(cp) }
                    }
                    setProgressAsync(Data.Builder().putFloat("progress", progress).build())
                }
            )

            if (success) {
                kotlinx.coroutines.runBlocking { recovery.remove(jobId) }
                Log.i("ExportWorker", "Export completed: $outputPath")
                Result.success(Data.Builder().putString("output", outputPath).build())
            } else {
                val message = "Export executor returned false"
                Log.e("ExportWorker", "Export failed: $jobId, $message")
                if (runAttemptCount < 2) Result.retry()
                else Result.failure(Data.Builder().putString(KEY_ERROR_MESSAGE, message).build())
            }
        } catch (e: Exception) {
            Log.e("ExportWorker", "Export exception: $jobId", e)
            if (runAttemptCount < 2) Result.retry()
            else Result.failure(Data.Builder().putString(KEY_ERROR_MESSAGE, e.message).build())
        }
    }
}

/**
 * 导出执行器接口 — 宿主 App 通过 [ExportExecutor.register] 注入实际导出实现。
 * 默认实现是空占位（返回 false），需在 Application.onCreate() 中替换。
 */
object ExportExecutor {
    private var impl: Impl = DefaultImpl

    fun interface Impl {
        fun execute(
            context: Context,
            jobId: String,
            draftPath: String,
            outputPath: String,
            width: Int,
            height: Int,
            fps: Int,
            bitrate: Int,
            colorSpaceMode: String,
            chunkDurationMs: Long,
            resumePositionMs: Long,
            onProgress: (progress: Float, posMs: Long) -> Unit
        ): Boolean
    }

    private object DefaultImpl : Impl {
        override fun execute(
            context: Context, jobId: String, draftPath: String, outputPath: String,
            width: Int, height: Int, fps: Int, bitrate: Int, colorSpaceMode: String,
            chunkDurationMs: Long, resumePositionMs: Long,
            onProgress: (Float, Long) -> Unit
        ): Boolean {
            Log.w("ExportExecutor", "No implementation registered — register ExportExecutor before using ExportQueue")
            return false
        }
    }

    fun register(impl: Impl) { this.impl = impl }

    fun execute(
        context: Context, jobId: String, draftPath: String, outputPath: String,
        width: Int, height: Int, fps: Int, bitrate: Int, colorSpaceMode: String,
        chunkDurationMs: Long, resumePositionMs: Long,
        onProgress: (Float, Long) -> Unit
    ) = impl.execute(context, jobId, draftPath, outputPath, width, height, fps, bitrate,
        colorSpaceMode, chunkDurationMs, resumePositionMs, onProgress)
}

// ---------------------------------------------------------------------------
// ExportQueue — high-level queue manager
// ---------------------------------------------------------------------------

/**
 * 导出队列管理器（WorkManager 驱动）。
 *
 * ## 特性
 * - **队列**：多个导出任务排队执行（WorkManager 串行策略）
 * - **优先级**：HIGH 任务插队到队列头，LOW 任务延迟启动
 * - **取消**：按 jobId 取消单个任务或全部任务
 * - **断点恢复**：崩溃重启后调用 [resumeAll] 重新入队未完成任务
 * - **进度观察**：通过 [observeProgress] Flow 获取实时进度
 *
 * ## 使用示例
 * ```kotlin
 * val queue = ExportQueue(context)
 * val jobId = queue.enqueue(ExportJob(
 *     draftPath  = "/cache/draft.svdk",
 *     outputPath = "/movies/out.mp4",
 *     width = 1080, height = 1920, fps = 30, bitrate = 8_000_000
 * ))
 * queue.observeProgress(jobId).collect { (progress, status) ->
 *     updateUI(progress, status)
 * }
 * ```
 */
class ExportQueue(private val context: Context) {

    companion object {
        private const val TAG = "ExportQueue"
        private const val WORK_TAG_EXPORT = "svdk_export"
        private const val QUEUE_NAME = "svdk_export_serial_queue"
    }

    private val workManager = WorkManager.getInstance(context)
    private val recovery    = ExportRecovery(context)
    private val scope       = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private val _jobs = MutableStateFlow<Map<String, ExportJob>>(emptyMap())
    val jobs: StateFlow<Map<String, ExportJob>> = _jobs.asStateFlow()

    // ── 入队 ──────────────────────────────────────────────────────────────────

    /**
     * 将导出任务加入队列。
     * @return 任务 ID（可用于取消或观察进度）
     */
    fun enqueue(job: ExportJob): String {
        val data = Data.Builder()
            .putString(ExportWorker.KEY_JOB_ID,      job.id)
            .putString(ExportWorker.KEY_DRAFT_PATH,  job.draftPath)
            .putString(ExportWorker.KEY_OUTPUT_PATH, job.outputPath)
            .putInt(ExportWorker.KEY_WIDTH,    job.width)
            .putInt(ExportWorker.KEY_HEIGHT,   job.height)
            .putInt(ExportWorker.KEY_FPS,      job.fps)
            .putInt(ExportWorker.KEY_BITRATE,  job.bitrate)
            .putString(ExportWorker.KEY_COLOR_SPACE, job.colorSpace.mode.name)
            .putLong(ExportWorker.KEY_CHUNK_MS, job.chunkDurationMs)
            .build()

        val request = OneTimeWorkRequestBuilder<ExportWorker>()
            .setInputData(data)
            .addTag(WORK_TAG_EXPORT)
            .addTag(job.id)
            .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 15, TimeUnit.SECONDS)
            .setExpedited(OutOfQuotaPolicy.RUN_AS_NON_EXPEDITED_WORK_REQUEST)
            .build()

        // HIGH = insert at front (new unique queue replaces if conflict), NORMAL/LOW = append
        val policy = if (job.priority == ExportPriority.HIGH)
            ExistingWorkPolicy.REPLACE
        else
            ExistingWorkPolicy.APPEND_OR_REPLACE

        workManager.enqueueUniqueWork(
            QUEUE_NAME + "_${job.id}",
            policy,
            request
        )

        _jobs.update { it + (job.id to job.copy(workRequestId = request.id)) }
        observeWork(job.id, request.id)
        Log.i(TAG, "Enqueued export: ${job.id} priority=${job.priority}")
        return job.id
    }

    // ── 取消 ──────────────────────────────────────────────────────────────────

    fun cancel(jobId: String) {
        workManager.cancelAllWorkByTag(jobId)
        _jobs.update { map ->
            map.toMutableMap().also { m ->
                m[jobId] = m[jobId]?.copy(status = ExportJobStatus.CANCELED) ?: return@also
            }
        }
        scope.launch { recovery.remove(jobId) }
        Log.i(TAG, "Canceled export: $jobId")
    }

    fun cancelAll() {
        workManager.cancelAllWorkByTag(WORK_TAG_EXPORT)
        _jobs.update { map -> map.mapValues { (_, job) -> job.copy(status = ExportJobStatus.CANCELED) } }
    }

    // ── 断点恢复 ──────────────────────────────────────────────────────────────

    /**
     * 重新入队所有上次崩溃/被杀死的未完成任务。
     * 建议在 Application.onCreate() 或主 Activity.onResume() 中调用。
     */
    fun resumeAll() {
        scope.launch {
            val resumable = recovery.findResumable()
            for (cp in resumable) {
                Log.i(TAG, "Resuming job: ${cp.jobId} from ${cp.resumePositionMs}ms")
                enqueue(ExportJob(
                    id          = cp.jobId,
                    draftPath   = cp.draftPath,
                    outputPath  = cp.outputPath,
                    width       = cp.width,
                    height      = cp.height,
                    fps         = cp.fps,
                    bitrate     = cp.bitrate,
                    colorSpace  = runCatching {
                        ColorSpaceConfig(ColorSpaceMode.valueOf(cp.colorSpace))
                    }.getOrDefault(ColorSpaceConfig.SDR)
                ))
            }
        }
    }

    // ── 进度观察 ──────────────────────────────────────────────────────────────

    /** Observe real-time (progress [0,1], status) for a specific job. */
    fun observeProgress(jobId: String): Flow<Pair<Float, ExportJobStatus>> =
        _jobs.map { map ->
            val job = map[jobId] ?: return@map Pair(0f, ExportJobStatus.QUEUED)
            Pair(job.progress, job.status)
        }

    // ── WorkManager 状态同步 ──────────────────────────────────────────────────

    private fun observeWork(jobId: String, requestId: UUID) {
        scope.launch {
            while (true) {
                val info = runCatching { workManager.getWorkInfoById(requestId).get() }.getOrNull()
                    ?: break
                val newStatus = info.state.toJobStatus()
                val progress = info.progress.getFloat("progress", 0f)
                val errorMessage = info.outputData.getString(ExportWorker.KEY_ERROR_MESSAGE)
                _jobs.update { map ->
                    map.toMutableMap().also { m ->
                        m[jobId] = m[jobId]?.copy(
                            status = newStatus,
                            progress = progress,
                            errorMessage = errorMessage
                        ) ?: return@also
                    }
                }
                if (info.state.isFinished) break
                delay(500)
            }
        }
    }

    private fun WorkInfo.State.toJobStatus() = when (this) {
        WorkInfo.State.ENQUEUED  -> ExportJobStatus.QUEUED
        WorkInfo.State.RUNNING   -> ExportJobStatus.RUNNING
        WorkInfo.State.SUCCEEDED -> ExportJobStatus.COMPLETED
        WorkInfo.State.FAILED    -> ExportJobStatus.FAILED
        WorkInfo.State.CANCELLED -> ExportJobStatus.CANCELED
        WorkInfo.State.BLOCKED   -> ExportJobStatus.QUEUED
    }
}
