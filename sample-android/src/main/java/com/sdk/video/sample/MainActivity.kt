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
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.sdk.video.sample.state.AppViewModel
import com.sdk.video.sample.ui.HomeScaffold
import com.sdk.video.timeline.TimelineManager
import kotlinx.coroutines.launch
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
            start = { startRecording() },
            stop  = { stopRecording() }
        )
        viewModel.setSoftwareDecoderAvailable(TimelineManager.queryIsSoftwareDecoderAvailable())

        if (allPermissionsGranted()) startCamera()
        else ActivityCompat.requestPermissions(
            this, REQUIRED_PERMISSIONS, REQUEST_CODE_PERMISSIONS)

        // React to camera-facing toggle from CaptureScreen.
        lifecycleScope.launch {
            viewModel.useFrontCamera.collect { useFront ->
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
        val cameraProviderFuture = ProcessCameraProvider.getInstance(this)
        cameraProviderFuture.addListener({
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

        val moviesDir = getExternalFilesDir(Environment.DIRECTORY_MOVIES)
        if (moviesDir != null && !moviesDir.exists()) moviesDir.mkdirs()

        val timeStamp: String = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val outputFile = File(moviesDir, "VideoSDK_$timeStamp.mp4")
        lastOutputFile = outputFile

        val config = VideoExportConfig(
            width = safeSize.width,
            height = safeSize.height,
            fps = 30,
            videoBitrate = 10_000_000,
            audioBitrate = 128_000,
            iFrameInterval = 1,
            outputPath = outputFile.absolutePath
        )

        fm.scope.launch {
            val result = fm.startVideoRecording(config)
            runOnUiThread {
                if (result.isSuccess) {
                    viewModel.setRecordingState(true)
                    Toast.makeText(this@MainActivity,
                        "Recording started", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this@MainActivity,
                        "Failed to start recording: ${result.exceptionOrNull()?.message}",
                        Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun stopRecording() {
        val fm = filterManager
        fm?.scope?.launch {
            fm.stopVideoRecording()
            val file = lastOutputFile
            val ok = file != null && file.exists() && file.length() > 0
            runOnUiThread {
                viewModel.setRecordingState(false)
                if (ok) {
                    Toast.makeText(this@MainActivity,
                        "Saved: ${file?.name}\n${file?.length()?.div(1024)} KB",
                        Toast.LENGTH_LONG).show()
                    Log.i(TAG, "Recording successful: ${file?.absolutePath}")
                } else {
                    Toast.makeText(this@MainActivity,
                        "Recording failed or file empty", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        ContextCompat.checkSelfPermission(baseContext, it) == PackageManager.PERMISSION_GRANTED
    }

    override fun onDestroy() {
        super.onDestroy()
        cameraExecutor.shutdown()
        filterManager?.let { fm -> fm.release() }
        filterManager = null
        viewModel.bindFilterManager(null)
    }
}
