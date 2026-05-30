package com.sdk.video.sample

import com.sdk.video.*
import com.sdk.video.InternalApi

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Environment
import android.util.Log
import android.util.Size
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.camera.core.CameraSelector
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.core.ImageCapture
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.sdk.video.sample.core.model.AppViewModel
import com.sdk.video.sample.ui.HomeScaffold
import com.sdk.video.timeline.TimelineManager
import kotlinx.coroutines.launch
import kotlinx.coroutines.isActive
import kotlinx.coroutines.delay
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

@OptIn(InternalApi::class)
class MainActivity : ComponentActivity() {

    private val viewModel: AppViewModel by viewModels()

    private var filterManager: VideoFilterManager? = null
    private var activeCameraResolution: Size? = null
    private var lastOutputFile: File? = null
    private var cameraSelector: CameraSelector = CameraSelector.DEFAULT_FRONT_CAMERA
    private lateinit var cameraExecutor: ExecutorService

    private var segmentRecorder: com.sdk.video.capture.SegmentRecorder? = null
    private var photoCaptureMode: com.sdk.video.capture.PhotoCaptureMode? = null
    private var countdownTimer: android.os.CountDownTimer? = null
    private var progressJob: kotlinx.coroutines.Job? = null

    companion object {
        private const val REQUEST_CODE_PERMISSIONS = 10
        private val REQUIRED_PERMISSIONS = arrayOf(
            Manifest.permission.CAMERA,
            Manifest.permission.RECORD_AUDIO
        )
        private const val TAG = "MainActivity"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        cameraExecutor = Executors.newSingleThreadExecutor()

        // Wire recording actions + diagnostic info into the shared ViewModel.
        viewModel.bindRecordingActions(
            start = { handleStartRecordTrigger() },
            stop  = { handleStopRecordTrigger() },
            deleteLast = { handleDeleteLastSegment() },
            finalize = { handleFinalizeSegments() },
            cancelAll = { handleCancelAllSegments() }
        )
        viewModel.setSoftwareDecoderAvailable(TimelineManager.queryIsSoftwareDecoderAvailable())

        // 读取业务配置启动时的镜头方向（默认前置，支持前后置设置）
        val defaultFront = intent.getBooleanExtra("default_front_camera", true)
        viewModel.initCameraFacing(defaultFront)

        if (allPermissionsGranted()) startCamera()
        else ActivityCompat.requestPermissions(
            this, REQUIRED_PERMISSIONS, REQUEST_CODE_PERMISSIONS)

        // React to camera-facing toggle from CaptureScreen.
        lifecycleScope.launch {
            viewModel.useFrontCamera.collect { useFront ->
                if (viewModel.isRecording.value) return@collect // 录制中不支持翻转摄像头
                val target = if (useFront) CameraSelector.DEFAULT_FRONT_CAMERA
                             else           CameraSelector.DEFAULT_BACK_CAMERA
                if (target != cameraSelector) {
                    cameraSelector = target
                    if (allPermissionsGranted()) startCamera()
                }
            }
        }

        setContent { HomeScaffold(viewModel) }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_CODE_PERMISSIONS && allPermissionsGranted()) startCamera()
    }

    private fun startCamera() {
        // Task 3: Decouple structural operations from the Android UI main thread.
        // Use a background thread for CameraX provider acquisition and binding negotiation.
        val cameraProviderFuture = ProcessCameraProvider.getInstance(this)
        cameraProviderFuture.addListener({
            lifecycleScope.launch(Dispatchers.Default) {
                val cameraProvider: ProcessCameraProvider = cameraProviderFuture.get()

                val resolutionSelector = ResolutionSelector.Builder()
                .setResolutionStrategy(
                    ResolutionStrategy(
                        Size(1080, 1920),
                        ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER
                    )
                )
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY)
                .build()

            val preview = Preview.Builder()
                .setResolutionSelector(resolutionSelector)
                .build()

            val moviesDir = getExternalFilesDir(Environment.DIRECTORY_MOVIES) ?: cacheDir
            val photoMode = com.sdk.video.capture.PhotoCaptureMode(this@MainActivity, moviesDir)
            photoCaptureMode = photoMode
            val imageCapture = photoMode.buildUseCase()

            preview.setSurfaceProvider { request ->
                val resolution = request.resolution
                Log.d(TAG, "CameraX negotiated resolution: ${resolution.width}x${resolution.height}")
                activeCameraResolution = resolution

                // Tear down the previous engine before binding a new resolution.
                val oldManager = filterManager
                oldManager?.scope?.launch { oldManager.release() }

                val newManager = VideoFilterManager(
                    this@MainActivity, resolution.width, resolution.height
                )

                // Publish to all screens via the shared ViewModel.
                viewModel.bindFilterManager(newManager)

                newManager.scope.launch {
                    try {
                        val surface = newManager.awaitInputSurface()
                        request.provideSurface(surface, cameraExecutor) {
                            surface.release()
                            newManager.release()
                        }
                        viewModel.refreshDeviceCaps()
                    } catch (e: Exception) {
                        Log.e(TAG, "Failed to provide surface", e)
                        runOnUiThread {
                            Toast.makeText(this@MainActivity,
                                "Engine Error: ${e.message}", Toast.LENGTH_LONG).show()
                        }
                    }
                }

                filterManager = newManager
            }

            runOnUiThread {
                try {
                    cameraProvider.unbindAll()
                    val camera = cameraProvider.bindToLifecycle(this@MainActivity, cameraSelector, preview, imageCapture)
                    viewModel.bindCamera(camera)
                } catch (exc: Exception) {
                    Log.e(TAG, "Use case binding failed", exc)
                    Toast.makeText(this@MainActivity, "Camera failed", Toast.LENGTH_SHORT).show()
                }
            }
        }, ContextCompat.getMainExecutor(this))
    }

    private fun getNewOutputConfig(): VideoExportConfig {
        val safeSize = activeCameraResolution ?: Size(1080, 1920)
        val moviesDir = getExternalFilesDir(Environment.DIRECTORY_MOVIES) ?: cacheDir
        val timeStamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val outputFile = File(moviesDir, "VideoSDK_$timeStamp.mp4")
        lastOutputFile = outputFile
        return VideoExportConfig(
            width = safeSize.width,
            height = safeSize.height,
            fps = 30,
            videoBitrate = 10_000_000,
            audioBitrate = 128_000,
            iFrameInterval = 1,
            outputPath = outputFile.absolutePath
        )
    }

    private fun handleStartRecordTrigger() {
        val seconds = viewModel.countdownSeconds.value
        if (seconds > 0) {
            viewModel.setCountdownProgress(seconds)
            countdownTimer?.cancel()
            countdownTimer = object : android.os.CountDownTimer(seconds * 1000L, 1000L) {
                override fun onTick(millisUntilFinished: Long) {
                    val currentProgress = (millisUntilFinished / 1000L).toInt() + 1
                    viewModel.setCountdownProgress(currentProgress)
                }

                override fun onFinish() {
                    viewModel.setCountdownProgress(-1)
                    startRecordingOrCapture()
                }
            }.start()
        } else {
            startRecordingOrCapture()
        }
    }

    private fun startRecordingOrCapture() {
        val isPhoto = viewModel.maxDurationMs.value == 0L
        if (isPhoto) {
            capturePhoto()
        } else {
            startSegmentRecording()
        }
    }

    private fun capturePhoto() {
        val pm = photoCaptureMode ?: return
        val flashOn = viewModel.torchEnabled.value
        pm.setFlashMode(if (flashOn) ImageCapture.FLASH_MODE_ON else ImageCapture.FLASH_MODE_OFF)

        runOnUiThread {
            Toast.makeText(this, "正在拍照...", Toast.LENGTH_SHORT).show()
        }
        pm.capture(ContextCompat.getMainExecutor(this)) { file, error ->
            if (file != null) {
                exportPhotoAsVideo(file)
            } else {
                Toast.makeText(this@MainActivity, "拍照失败: $error", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun startSegmentRecording() {
        val fm = filterManager ?: return
        val currentTotalMs = segmentRecorder?.totalDurationMs ?: 0L
        val maxMs = viewModel.maxDurationMs.value
        if (currentTotalMs >= maxMs) {
            Toast.makeText(this, "已达到录制时长上限", Toast.LENGTH_SHORT).show()
            return
        }

        if (segmentRecorder == null || segmentRecorder?.segmentCount == 0) {
            segmentRecorder = com.sdk.video.capture.SegmentRecorder(fm, getNewOutputConfig())
        }

        fm.scope.launch {
            val result = segmentRecorder?.startSegment()
            runOnUiThread {
                if (result?.isSuccess == true) {
                    viewModel.setRecordingState(true)
                    val startTimeMs = System.currentTimeMillis()
                    startProgressPolling(startTimeMs, currentTotalMs)
                } else {
                    Toast.makeText(this@MainActivity,
                        "开始录像失败: ${result?.exceptionOrNull()?.message}",
                        Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun startProgressPolling(startTimeMs: Long, previousTotalMs: Long) {
        progressJob?.cancel()
        progressJob = lifecycleScope.launch {
            while (isActive) {
                val elapsed = System.currentTimeMillis() - startTimeMs
                val rate = viewModel.speedRate.value
                val elapsedScaled = (elapsed * rate).toLong()

                val currentTotal = previousTotalMs + elapsedScaled
                val currentCount = (segmentRecorder?.segmentCount ?: 0) + 1

                viewModel.setSegmentStats(currentCount, currentTotal)

                val maxMs = viewModel.maxDurationMs.value
                if (currentTotal >= maxMs) {
                    handleStopRecordTrigger()
                    break
                }
                delay(50)
            }
        }
    }

    private fun handleStopRecordTrigger() {
        countdownTimer?.cancel()
        viewModel.setCountdownProgress(-1)

        progressJob?.cancel()
        progressJob = null

        val recorder = segmentRecorder
        if (recorder != null && recorder.isRecording) {
            val fm = filterManager
            fm?.scope?.launch {
                val result = recorder.stopSegment()
                runOnUiThread {
                    viewModel.setRecordingState(false)
                    if (result.isSuccess) {
                        val seg = result.getOrThrow()
                        viewModel.setSegmentStats(recorder.segmentCount, recorder.totalDurationMs)
                        Toast.makeText(this@MainActivity, "分段 ${seg.index + 1} 录制完成", Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(this@MainActivity,
                            "停止录像失败: ${result.exceptionOrNull()?.message}",
                            Toast.LENGTH_SHORT).show()
                    }
                }
            }
        }
    }

    private fun handleDeleteLastSegment() {
        val recorder = segmentRecorder ?: return
        val fm = filterManager ?: return
        fm.scope.launch {
            recorder.deleteLastSegment()
            runOnUiThread {
                viewModel.setSegmentStats(recorder.segmentCount, recorder.totalDurationMs)
                Toast.makeText(this@MainActivity, "已删除最后一段", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun handleFinalizeSegments() {
        val recorder = segmentRecorder ?: return
        val fm = filterManager ?: return
        fm.scope.launch {
            val result = recorder.finalize()
            runOnUiThread {
                if (result.isSuccess) {
                    val finalPath = result.getOrThrow()
                    val file = File(finalPath)
                    viewModel.setSegmentStats(0, 0L)
                    segmentRecorder = null
                    Toast.makeText(this@MainActivity, "合并完成: ${file.name}", Toast.LENGTH_LONG).show()
                } else {
                    Toast.makeText(this@MainActivity, "合并失败: ${result.exceptionOrNull()?.message}", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun handleCancelAllSegments() {
        val recorder = segmentRecorder ?: return
        val fm = filterManager ?: return
        fm.scope.launch {
            recorder.cancel()
            runOnUiThread {
                viewModel.setSegmentStats(0, 0L)
                segmentRecorder = null
                Toast.makeText(this@MainActivity, "已清空分段", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun exportPhotoAsVideo(photoFile: File) {
        val fm = filterManager ?: return
        val moviesDir = getExternalFilesDir(Environment.DIRECTORY_MOVIES) ?: cacheDir
        val timeStamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val videoFile = File(moviesDir, "PhotoVideo_$timeStamp.mp4")

        val width = activeCameraResolution?.width ?: 1080
        val height = activeCameraResolution?.height ?: 1920

        val config = VideoExportConfig(
            width = width,
            height = height,
            fps = 30,
            videoBitrate = 10_000_000,
            audioBitrate = 128_000,
            iFrameInterval = 1,
            outputPath = videoFile.absolutePath
        )

        val tm = com.sdk.video.timeline.TimelineManager(width, height, 30)
        tm.addTrack(0, com.sdk.video.timeline.TimelineManager.TrackType.MAIN_VIDEO)
        tm.addClip(
            zIndex = 0,
            clipId = "photo_clip",
            sourcePath = photoFile.absolutePath,
            mediaType = com.sdk.video.timeline.TimelineManager.MediaType.IMAGE,
            trimInUs = 0L,
            trimOutUs = 3_000_000L,
            timelineInUs = 0L
        )

        val exporter = com.sdk.video.timeline.TimelineExporter()
        exporter.configure(config)

        runOnUiThread {
            Toast.makeText(this, "正在将照片转为3秒视频...", Toast.LENGTH_SHORT).show()
        }

        exporter.exportAsync(
            timeline = tm,
            renderEngine = fm.renderEngine,
            onProgress = {},
            onComplete = { errorCode, message ->
                tm.release()
                exporter.release()
                runOnUiThread {
                    if (errorCode == 0 && videoFile.exists() && videoFile.length() > 0) {
                        Toast.makeText(this@MainActivity, "保存3s视频: ${videoFile.name}", Toast.LENGTH_LONG).show()
                        Log.i(TAG, "Photo video export success: ${videoFile.absolutePath}")
                    } else {
                        Toast.makeText(this@MainActivity, "转视频失败: $message", Toast.LENGTH_LONG).show()
                        Log.e(TAG, "Photo video export failed: $errorCode - $message")
                    }
                }
            }
        )
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        ContextCompat.checkSelfPermission(baseContext, it) == PackageManager.PERMISSION_GRANTED
    }

    override fun onDestroy() {
        super.onDestroy()
        cameraExecutor.shutdown()
        countdownTimer?.cancel()
        progressJob?.cancel()
        filterManager?.let { fm -> fm.release() }
        filterManager = null
        segmentRecorder = null
        photoCaptureMode?.release()
        photoCaptureMode = null
        viewModel.bindFilterManager(null)
        viewModel.bindCamera(null)
    }
}
