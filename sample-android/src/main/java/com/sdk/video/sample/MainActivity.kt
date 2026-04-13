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
import androidx.camera.core.CameraSelector
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.launch
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

@OptIn(InternalApi::class)
class MainActivity : ComponentActivity() {

    private var filterManager: VideoFilterManager? = null
    private var activeCameraResolution: Size? = null
    private lateinit var cameraExecutor: ExecutorService

    // Compose State
    private var isRecordingState = mutableStateOf(false)
    private var performanceMs = mutableStateOf(0L)
    private var droppedFrames = mutableStateOf(0)

    companion object {
        private const val REQUEST_CODE_PERMISSIONS = 10
        private val REQUIRED_PERMISSIONS = arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        private const val TAG = "MainActivity"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        cameraExecutor = Executors.newSingleThreadExecutor()

        if (allPermissionsGranted()) {
            startCamera()
        } else {
            ActivityCompat.requestPermissions(this, REQUIRED_PERMISSIONS, REQUEST_CODE_PERMISSIONS)
        }

        setContent {
            Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {
                // UI 层只接收 Facade，生命周期由下方的 CameraX 严格托管
                filterManager?.let { fm ->
                    FilterCameraPreview(
                        filterManager = fm,
                        modifier = Modifier.fillMaxSize()
                    )
                }

                Column(
                    modifier = Modifier
                        .align(Alignment.TopStart)
                        .padding(16.dp)
                ) {
                    Text(
                        text = "Frame: ${performanceMs.value} ms",
                        color = if (performanceMs.value > 16) Color.Red else Color.Green
                    )
                    Text(
                        text = "Dropped: ${droppedFrames.value}",
                        color = if (droppedFrames.value > 0) Color.Red else Color.White
                    )
                }

                Button(
                    onClick = {
                        if (isRecordingState.value) stopRecording() else startRecording()
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (isRecordingState.value) Color.DarkGray else Color.Red
                    ),
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .padding(32.dp)
                ) {
                    Text(if (isRecordingState.value) "STOP RECORDING" else "START RECORDING")
                }
            }
        }
    }

    private fun startCamera() {
        val cameraProviderFuture = ProcessCameraProvider.getInstance(this)
        cameraProviderFuture.addListener({
            val cameraProvider: ProcessCameraProvider = cameraProviderFuture.get()

            val resolutionSelector = ResolutionSelector.Builder()
                .setResolutionStrategy(ResolutionStrategy(Size(1080, 1920), ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER))
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY)
                .build()

            val preview = Preview.Builder()
                .setResolutionSelector(resolutionSelector)
                .build()

            preview.setSurfaceProvider { request ->
                val resolution = request.resolution
                Log.d(TAG, "CameraX negotiated resolution: ${resolution.width}x${resolution.height}")
                activeCameraResolution = resolution

                // 清除之前的实例（如果有），防止多开
                // Fix Risk B: Capture old instance locally before releasing to prevent race conditions
                val oldManager = filterManager
                oldManager?.scope?.launch { oldManager.release() }

                val newManager = VideoFilterManager(this@MainActivity, resolution.width, resolution.height)

                // Observe unified performance metrics
                newManager.scope.launch {
                    newManager.performanceMetrics.collect { metrics ->
                        metrics?.let {
                            runOnUiThread {
                                performanceMs.value = it.averageFrameTimeMs.toLong()
                                droppedFrames.value = it.droppedFrames
                            }
                        }
                    }
                }

                // 启动引擎的唯一合法入口 (移交 GLSurfaceView 内部初始化)
                newManager.scope.launch {
                    try {
                        // 摒弃不稳定的 delay(500)，改为事件驱动：挂起直到底层 GLSurfaceView 真正创建好 EGL Context 和 OES 纹理
                        val surface = newManager.awaitInputSurface()

                        request.provideSurface(surface, cameraExecutor) { result ->
                            // 核心逻辑：当 CameraX 确认销毁旧的 Surface 时，连带销毁其绑定的引擎 FBO 资源
                            surface.release()
                            newManager.release()
                        }
                    } catch (e: Exception) {
                        Log.e(TAG, "Failed to provide surface", e)
                    }
                }

                filterManager = newManager
            }

            val cameraSelector = CameraSelector.DEFAULT_FRONT_CAMERA
            try {
                cameraProvider.unbindAll()
                cameraProvider.bindToLifecycle(this, cameraSelector, preview)
            } catch (exc: Exception) {
                Log.e(TAG, "Use case binding failed", exc)
                Toast.makeText(this, "Camera failed", Toast.LENGTH_SHORT).show()
            }

        }, ContextCompat.getMainExecutor(this))
    }

    private fun startRecording() {
        val safeSize = activeCameraResolution ?: return
        val fm = filterManager ?: return

        val moviesDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES)
        if (!moviesDir.exists()) moviesDir.mkdirs()

        val timeStamp: String = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val outputFile = File(moviesDir, "VideoSDK_$timeStamp.mp4")

        val config = VideoExportConfig(
            width = safeSize.width,
            height = safeSize.height,
            fps = 30,
            videoBitrate = 10_000_000,
            audioBitrate = 128_000,
            iFrameInterval = 1,
            outputPath = outputFile.absolutePath
        )

        val result = fm.startVideoRecording(config)
        if (result.isSuccess) {
            isRecordingState.value = true
            Toast.makeText(this, "Recording started", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Failed to start recording: ${result.exceptionOrNull()?.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun stopRecording() {
        val fm = filterManager
        fm?.scope?.launch {
            fm.stopVideoRecording()
            runOnUiThread {
                isRecordingState.value = false
                Toast.makeText(this@MainActivity, "Video saved!", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        ContextCompat.checkSelfPermission(baseContext, it) == PackageManager.PERMISSION_GRANTED
    }

    override fun onDestroy() {
        super.onDestroy()
        cameraExecutor.shutdown()
        // 兜底安全释放：即使 CameraX 没有正常触发 request.provideSurface 的销毁回调，
        // 在 Activity 彻底死亡时也将 GL 资源清理干净。
        filterManager?.let { fm ->
            fm.release()
        }
        filterManager = null
    }
}
