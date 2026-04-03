package com.sdk.video

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
import androidx.compose.foundation.shape.CircleShape
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

class MainActivity : ComponentActivity() {

    private var filterManager: VideoFilterManager? = null
    private var videoEncoder: VideoEncoder? = null
    private var activeCameraResolution: Size? = null
    private lateinit var cameraExecutor: ExecutorService

    // Compose State
    private var isRecordingState = mutableStateOf(false)
    private var performanceMs = mutableStateOf(0L)

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
                // UI 层只接收 Facade，不需要知道 RenderEngine 的存在
                filterManager?.let { fm ->
                    FilterCameraPreview(
                        filterManager = fm,
                        modifier = Modifier.fillMaxSize()
                    )
                }

                // 性能指标监控
                Text(
                    text = "${performanceMs.value} ms",
                    color = if (performanceMs.value > 16) Color.Red else Color.Green,
                    modifier = Modifier
                        .align(Alignment.TopStart)
                        .padding(16.dp)
                )

                // 录制控制按钮
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

                // 彻底通过 Facade (VideoFilterManager) 统一生命周期，废弃 RenderEngine 直接暴露
                filterManager?.scope?.launch { filterManager?.release() }

                val newManager = VideoFilterManager(resolution.width, resolution.height)

                // 监听性能指标
                newManager.setOnPerformanceUpdateListener { durationMs ->
                    performanceMs.value = durationMs
                }

                // 启动引擎
                newManager.scope.launch {
                    try {
                        newManager.initialize()
                        // 为了前置摄像头，自动水平翻转等基础操作可以通过 UpdateParameter 或内置 Filter 搞定
                        // 此处简单添加必备的基础滤镜
                        val surface = newManager.getInputSurface()
                        if (surface != null) {
                            request.provideSurface(surface, cameraExecutor) {
                                surface.release()
                            }
                        }
                    } catch (e: Exception) {
                        Log.e(TAG, "Failed to initialize VideoFilterManager", e)
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

        videoEncoder = VideoEncoder(filterManager = fm, width = safeSize.width, height = safeSize.height)
        val recordingSurface = videoEncoder?.startRecording(outputFile.absolutePath)

        if (recordingSurface != null) {
            fm.scope.launch {
                fm.startVideoRecording(recordingSurface)
            }
            isRecordingState.value = true
        } else {
            Toast.makeText(this, "Failed to start recording", Toast.LENGTH_SHORT).show()
        }
    }

    private fun stopRecording() {
        val fm = filterManager
        fm?.scope?.launch {
            fm.stopVideoRecording()
        }

        videoEncoder?.stopRecording()
        videoEncoder = null
        isRecordingState.value = false

        Toast.makeText(this, "Video saved!", Toast.LENGTH_SHORT).show()
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        ContextCompat.checkSelfPermission(baseContext, it) == PackageManager.PERMISSION_GRANTED
    }

    // [修复关键问题]：原本代码里可能写成了 super.onCreate() 导致无限递归或者泄漏
    override fun onDestroy() {
        super.onDestroy()
        cameraExecutor.shutdown()
        filterManager?.scope?.launch {
            filterManager?.release()
        }
    }
}
