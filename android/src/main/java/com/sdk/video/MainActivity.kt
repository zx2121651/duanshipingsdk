package com.sdk.video

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.Environment
import android.util.Size
import android.view.TextureView
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.core.CameraSelector
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

class MainActivity : AppCompatActivity() {

    private lateinit var textureView: TextureView
    private lateinit var tvPerformance: TextView
    private lateinit var seekBarSmoothing: SeekBar
    private lateinit var btnRecord: Button

    private var renderEngine: RenderEngine? = null
    private var videoEncoder: VideoEncoder? = null
    private var isRecording = false

    private lateinit var cameraExecutor: ExecutorService

    companion object {
        private const val REQUEST_CODE_PERMISSIONS = 10
        private val REQUIRED_PERMISSIONS = arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        textureView = findViewById(R.id.previewView)
        tvPerformance = findViewById(R.id.tvPerformance)
        seekBarSmoothing = findViewById(R.id.seekBarSmoothing)
        btnRecord = findViewById(R.id.btnRecord)

        cameraExecutor = Executors.newSingleThreadExecutor()

        if (allPermissionsGranted()) {
            startCamera()
        } else {
            ActivityCompat.requestPermissions(this, REQUIRED_PERMISSIONS, REQUEST_CODE_PERMISSIONS)
        }

        setupUI()
    }

    private fun setupUI() {
        seekBarSmoothing.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                // Map 0-100 to smoothing factor 0.0 - 15.0
                val factor = (progress / 100f) * 15.0f
                renderEngine?.updateParameterFloat("distanceNormalizationFactor", factor)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        btnRecord.setOnClickListener {
            if (isRecording) {
                stopRecording()
            } else {
                startRecording()
            }
        }
    }

    private fun startCamera() {
        textureView.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
                // Initialize the RenderEngine when TextureView is ready
                renderEngine = RenderEngine(width, height)
                renderEngine?.init()

                // Add the Skin Smoothing filter (Bilateral)
                renderEngine?.addFilter(RenderEngine.FILTER_TYPE_BILATERAL)

                // For front camera, we usually want to flip horizontally
                renderEngine?.setFlip(horizontal = true, vertical = false)

                // Setup Performance Callback
                renderEngine?.onPerformanceUpdateListener = { durationMs ->
                    runOnUiThread {
                        tvPerformance.text = "$durationMs ms"
                        if (durationMs > 16) {
                            tvPerformance.setTextColor(Color.RED)
                        } else {
                            tvPerformance.setTextColor(Color.GREEN)
                        }
                    }
                }

                // Bind CameraX to the engine's SurfaceTexture
                val cameraProviderFuture = ProcessCameraProvider.getInstance(this@MainActivity)
                cameraProviderFuture.addListener({
                    val cameraProvider: ProcessCameraProvider = cameraProviderFuture.get()
                    val preview = Preview.Builder()
                        .setTargetResolution(Size(1080, 1920))
                        .build()
                        .also {
                            it.setSurfaceProvider { request ->
                                // Provide the RenderEngine's SurfaceTexture to CameraX
                                val st = renderEngine?.getSurfaceTexture()
                                if (st != null) {
                                    st.setDefaultBufferSize(request.resolution.width, request.resolution.height)
                                    val engineSurface = android.view.Surface(st)
                                    request.provideSurface(engineSurface, cameraExecutor) { result ->
                                        engineSurface.release()
                                    }
                                }
                            }
                        }

                    val cameraSelector = CameraSelector.DEFAULT_FRONT_CAMERA
                    try {
                        cameraProvider.unbindAll()
                        cameraProvider.bindToLifecycle(this@MainActivity, cameraSelector, preview)
                    } catch (exc: Exception) {
                        Toast.makeText(this@MainActivity, "Camera failed", Toast.LENGTH_SHORT).show()
                    }
                }, ContextCompat.getMainExecutor(this@MainActivity))
            }

            override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {}
            override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
                renderEngine?.release()
                return true
            }
            override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
                // Optional: If we want to draw the result back to TextureView, we need an EGL context
                // wrapped around the TextureView's surface. For simplicity, RenderEngine can be modified
                // to draw to the current bound EGL surface (which is the TextureView if configured correctly).
            }
        }
    }

    private fun startRecording() {
        val moviesDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES)
        if (!moviesDir.exists()) {
            moviesDir.mkdirs()
        }

        val timeStamp: String = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val outputFile = File(moviesDir, "VideoSDK_$timeStamp.mp4")

        videoEncoder = VideoEncoder(width = 1080, height = 1920)
        val recordingSurface = videoEncoder?.startRecording(outputFile.absolutePath)

        if (recordingSurface != null) {
            renderEngine?.startRecording(recordingSurface)
            isRecording = true
            btnRecord.text = "STOP RECORDING"
            btnRecord.setBackgroundColor(Color.DKGRAY)
        } else {
            Toast.makeText(this, "Failed to start recording", Toast.LENGTH_SHORT).show()
        }
    }

    private fun stopRecording() {
        renderEngine?.stopRecording()
        videoEncoder?.stopRecording()
        videoEncoder = null
        isRecording = false
        btnRecord.text = "START RECORDING"
        btnRecord.setBackgroundColor(Color.RED)

        Toast.makeText(this, "Video saved to Movies folder!", Toast.LENGTH_LONG).show()
    }

    private fun allPermissionsGranted() = REQUIRED_PERMISSIONS.all {
        ContextCompat.checkSelfPermission(baseContext, it) == PackageManager.PERMISSION_GRANTED
    }

    override fun onDestroy() {
        super.onCreate()
        cameraExecutor.shutdown()
        renderEngine?.release()
    }
}
