package com.sdk.video

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.os.Environment
import android.util.Log
import android.util.Size
import android.view.Surface
import android.view.TextureView
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.core.CameraSelector
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
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

    // 动态保存 CameraX 协商后硬件支持的安全分辨率
    private var activeCameraResolution: Size? = null

    private lateinit var cameraExecutor: ExecutorService

    companion object {
        private const val REQUEST_CODE_PERMISSIONS = 10
        private val REQUIRED_PERMISSIONS = arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        private const val TAG = "MainActivity"
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
        val cameraProviderFuture = ProcessCameraProvider.getInstance(this)
        cameraProviderFuture.addListener({
            val cameraProvider: ProcessCameraProvider = cameraProviderFuture.get()

            // 1. 废弃硬编码，使用 ResolutionSelector 构建安全的回退策略
            // 1. 使用 CameraX 推荐的 ResolutionSelector 来做兼容性降级
            // 如果手机不支持请求的 1080p，则自动向下寻找最接近的安全分辨率 (Fallback)
            val resolutionSelector = ResolutionSelector.Builder()
                .setResolutionStrategy(
                    ResolutionStrategy(
                        Size(1080, 1920),
                        ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER
                    )
                )
                // 推荐优先保持 16:9 比例
                .setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY)
                .build()

            val preview = Preview.Builder()
                .setResolutionSelector(resolutionSelector)
                .build()

            // 2. 在得到真实分辨率后动态初始化渲染引擎
            preview.setSurfaceProvider { request ->
                // 获取 CameraX 与底层硬件协商后的实际安全分辨率
                val resolution = request.resolution
                Log.d(TAG, "CameraX negotiated resolution: ${resolution.width}x${resolution.height}")

                // 保存安全分辨率供 VideoEncoder 使用
                activeCameraResolution = resolution

                // 释放旧引擎（如果存在）
                renderEngine?.release()

                // 使用真实分辨率初始化 RenderEngine
                renderEngine = RenderEngine(resolution.width, resolution.height)
                renderEngine?.init()
                renderEngine?.addFilter(RenderEngine.FILTER_TYPE_BILATERAL)
                renderEngine?.setFlip(horizontal = true, vertical = false)

                renderEngine?.onPerformanceUpdateListener = { durationMs ->
                    runOnUiThread {
                        tvPerformance.text = "$durationMs ms"
                        tvPerformance.setTextColor(if (durationMs > 16) Color.RED else Color.GREEN)
                    }
                }

                val st = renderEngine?.getSurfaceTexture()
                if (st != null) {
                    st.setDefaultBufferSize(resolution.width, resolution.height)
                    val engineSurface = Surface(st)

                    // 将包含 TransformMatrix 信息的 Surface 提供给 CameraX
                    request.provideSurface(engineSurface, cameraExecutor) { result ->
                        engineSurface.release()
                        // 注意：这里不直接 release engine，交由 Activity 生命周期管理
                    }
                }
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
        // 确保已经拿到了安全的相机分辨率
        val safeSize = activeCameraResolution ?: return

        val moviesDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES)
        if (!moviesDir.exists()) moviesDir.mkdirs()

        val timeStamp: String = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val outputFile = File(moviesDir, "VideoSDK_$timeStamp.mp4")

        // 3. 动态配置编码器分辨率，杜绝 MediaCodec 异常
        videoEncoder = renderEngine?.let { VideoEncoder(renderEngine = it, width = safeSize.width, height = safeSize.height) }
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
        btnRecord.setBackgroundColor(Color.parseColor("#FF0000"))

        Toast.makeText(this, "Video saved!", Toast.LENGTH_SHORT).show()
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
