@file:OptIn(com.sdk.video.InternalApi::class)

package com.sdk.video.sample.core.model

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.sdk.video.RenderEngine
import com.sdk.video.VideoFilterManager
import com.sdk.video.VideoFilterType
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import androidx.camera.core.Camera

/**
 * Shared state holder for the Android sample app.
 *
 * This ViewModel keeps UI state in sync with the SDK-facing [VideoFilterManager]
 * and exposes state flows consumed by the feature modules.
 */
class AppViewModel : ViewModel() {

    private val _filterManager = MutableStateFlow<VideoFilterManager?>(null)
    val filterManager: StateFlow<VideoFilterManager?> = _filterManager.asStateFlow()

    private val _camera = MutableStateFlow<Camera?>(null)
    val camera: StateFlow<Camera?> = _camera.asStateFlow()

    private val _zoomRatio = MutableStateFlow(1f)
    val zoomRatio: StateFlow<Float> = _zoomRatio.asStateFlow()

    private val _exposureIndex = MutableStateFlow(0)
    val exposureIndex: StateFlow<Int> = _exposureIndex.asStateFlow()

    private val _exposureRange = MutableStateFlow(IntRange(0, 0))
    val exposureRange: StateFlow<IntRange> = _exposureRange.asStateFlow()

    private val _torchEnabled = MutableStateFlow(false)
    val torchEnabled: StateFlow<Boolean> = _torchEnabled.asStateFlow()

    /** 当前相机是否支持手电筒（仅后置摄像头有硬件闪光灯） */
    private val _hasTorch = MutableStateFlow(false)
    val hasTorch: StateFlow<Boolean> = _hasTorch.asStateFlow()

    fun bindFilterManager(fm: VideoFilterManager?) {
        _filterManager.value = fm
    }

    fun bindCamera(camera: Camera?) {
        _camera.value = camera
        if (camera != null) {
            val range = camera.cameraInfo.exposureState.exposureCompensationRange
            _exposureRange.value = IntRange(range.lower, range.upper)
            _hasTorch.value = camera.cameraInfo.hasFlashUnit()
        } else {
            _exposureRange.value = IntRange(0, 0)
            _hasTorch.value = false
        }
        // Reset states
        _zoomRatio.value = 1f
        _exposureIndex.value = 0
        _torchEnabled.value = false
    }

    fun setZoomRatio(ratio: Float) {
        _camera.value?.cameraControl?.setZoomRatio(ratio)
        _zoomRatio.value = ratio
    }

    fun setExposureIndex(index: Int) {
        _camera.value?.cameraControl?.setExposureCompensationIndex(index)
        _exposureIndex.value = index
    }

    fun toggleTorch() {
        val cam = _camera.value ?: return
        if (!cam.cameraInfo.hasFlashUnit()) {
            android.util.Log.w("AppViewModel", "toggleTorch: flash unit not available on this camera")
            return
        }
        val target = !_torchEnabled.value
        cam.cameraControl.enableTorch(target)
        _torchEnabled.value = target
    }

    private val _activeFilters = MutableStateFlow<List<VideoFilterType>>(emptyList())
    val activeFilters: StateFlow<List<VideoFilterType>> = _activeFilters.asStateFlow()

    fun toggleFilter(type: VideoFilterType) {
        val fm = _filterManager.value ?: return
        val current = _activeFilters.value
        val newList = if (current.contains(type)) current - type else current + type
        _activeFilters.value = newList

        viewModelScope.launch {
            fm.removeAllFilters()
            for (filter in newList) {
                fm.addFilter(filter)
            }
        }
    }

    private val _beautyEnabled = MutableStateFlow(false)
    val beautyEnabled: StateFlow<Boolean> = _beautyEnabled.asStateFlow()

    private val _smoothStrength = MutableStateFlow(0.6f)
    val smoothStrength: StateFlow<Float> = _smoothStrength.asStateFlow()

    private val _whitenStrength = MutableStateFlow(0.4f)
    val whitenStrength: StateFlow<Float> = _whitenStrength.asStateFlow()

    fun setBeautyEnabled(enabled: Boolean) {
        val fm = _filterManager.value ?: return
        _beautyEnabled.value = enabled
        viewModelScope.launch {
            val result = if (enabled) {
                fm.enableBeauty(_smoothStrength.value, _whitenStrength.value)
            } else {
                fm.disableBeauty()
            }
            if (result.isFailure) {
                // 原生失败时回滚 UI 状态
                _beautyEnabled.value = !enabled
                android.util.Log.e("AppViewModel",
                    "Beauty toggle failed: ${result.exceptionOrNull()?.message}")
            }
        }
    }

    // ── 美颜参数防抖 Job ────────────────────────────────────────────────
    // 滑杆拖动会高频触发 GL 调用，用 coroutine Job 做防抖（delay 后合并参数），
    // 避免 GL 队列塞满导致画面卡死。
    private var beautyDebounceJob: kotlinx.coroutines.Job? = null

    fun setSmoothStrength(value: Float) {
        _smoothStrength.value = value
        if (_beautyEnabled.value) scheduleBeautyDebounce()
    }

    fun setWhitenStrength(value: Float) {
        _whitenStrength.value = value
        if (_beautyEnabled.value) scheduleBeautyDebounce()
    }

    private fun scheduleBeautyDebounce() {
        beautyDebounceJob?.cancel()
        beautyDebounceJob = viewModelScope.launch {
            kotlinx.coroutines.delay(80)  // 80ms 防抖窗口
            val fm = _filterManager.value ?: return@launch
            fm.enableBeauty(_smoothStrength.value, _whitenStrength.value)
        }
    }

    private val _eyeScale = MutableStateFlow(0f)
    private val _faceSlim = MutableStateFlow(0f)
    private val _noseSlim = MutableStateFlow(0f)
    private val _chinV = MutableStateFlow(0f)

    val eyeScale: StateFlow<Float> = _eyeScale.asStateFlow()
    val faceSlim: StateFlow<Float> = _faceSlim.asStateFlow()
    val noseSlim: StateFlow<Float> = _noseSlim.asStateFlow()
    val chinV: StateFlow<Float> = _chinV.asStateFlow()

    // ── 人脸形变参数防抖 ────────────────────────────────────────────────
    private var reshapeDebounceJob: kotlinx.coroutines.Job? = null

    fun setReshape(
        eye: Float = _eyeScale.value,
        slim: Float = _faceSlim.value,
        nose: Float = _noseSlim.value,
        chin: Float = _chinV.value
    ) {
        _eyeScale.value = eye
        _faceSlim.value = slim
        _noseSlim.value = nose
        _chinV.value = chin

        // 防抖：取消上次挂起的 Job，delay 80ms 后合并提交一次
        reshapeDebounceJob?.cancel()
        reshapeDebounceJob = viewModelScope.launch {
            kotlinx.coroutines.delay(80)
            val fm = _filterManager.value ?: return@launch
            fm.setFaceReshape(
                eyeScale = eye,
                faceSlim = slim,
                noseSlim = nose,
                chinV = chin
            )
        }
    }

    private val _activeEffectId = MutableStateFlow<String?>(null)
    val activeEffectId: StateFlow<String?> = _activeEffectId.asStateFlow()

    private val loadedEffectIds = mutableSetOf<String>()

    fun activateEffect(id: String?) {
        _activeEffectId.value = id
        val fm = _filterManager.value ?: return
        viewModelScope.launch {
            if (id.isNullOrEmpty()) {
                fm.deactivateAllEffects()
            } else {
                fm.activateEffect(id)
            }
        }
    }

    fun loadAndActivateEffect(effectRoot: String, expectedId: String) {
        val fm = _filterManager.value ?: return
        viewModelScope.launch {
            if (!loadedEffectIds.contains(expectedId)) {
                val result = fm.loadEffect(effectRoot)
                val id = result.getOrNull().orEmpty()
                if (id.isNotEmpty()) {
                    loadedEffectIds.add(id)
                }
            }
            fm.activateEffect(expectedId)
            _activeEffectId.value = expectedId
        }
    }

    private val _filterParams = MutableStateFlow<Map<String, Float>>(emptyMap())
    val filterParams: StateFlow<Map<String, Float>> = _filterParams.asStateFlow()

    fun setFilterParam(key: String, value: Float) {
        _filterParams.value = _filterParams.value + (key to value)
        val fm = _filterManager.value ?: return
        viewModelScope.launch { fm.updateParameter(key, value) }
    }

    fun setFilterParam(key: String, value: Int) {
        val fm = _filterManager.value ?: return
        viewModelScope.launch { fm.updateParameter(key, value) }
    }

    private val _useFrontCamera = MutableStateFlow(true)
    val useFrontCamera: StateFlow<Boolean> = _useFrontCamera.asStateFlow()

    fun initCameraFacing(isFront: Boolean) {
        _useFrontCamera.value = isFront
    }

    fun toggleCameraFacing() {
        _useFrontCamera.value = !_useFrontCamera.value
    }

    // ── 拍摄分辨率 / 画质 ────────────────────────────────────────────────

    /**
     * 画质预设。存储两个尺寸：
     * - cameraWidth/cameraHeight: CameraX 请求的目标（传感器原始方向，通常横屏）
     * - engineWidth/engineHeight: 引擎 FBO 尺寸（用户方向，竖屏）
     *
     * Camera2/CameraX 按传感器方向（横屏）输出，OES2RGBFilter 旋转后引擎
     * 以竖屏处理，所以两个方向尺寸互换。
     */
    enum class CaptureResolutionPreset(
        val label: String,
        val cameraWidth: Int,
        val cameraHeight: Int,
        val engineWidth: Int,
        val engineHeight: Int,
        val bitrate: Int
    ) {
        AUTO(   "自动",   1920, 1080, 1080, 1920, 10_000_000),
        P720(   "720P",   1280,  720,  720, 1280,  5_000_000),
        P1080(  "1080P",  1920, 1080, 1080, 1920, 10_000_000),
        P2K(    "2K",     2560, 1440, 1440, 2560, 18_000_000),
        P4K(    "4K",     3840, 2160, 2160, 3840, 35_000_000);

        /** CameraX ResolutionSelector 使用的目标尺寸（传感器方向） */
        fun toCameraSize(): android.util.Size = android.util.Size(cameraWidth, cameraHeight)
        /** VideoFilterManager / 引擎 FBO 使用的尺寸（用户方向，竖屏） */
        fun toEngineSize(): android.util.Size = android.util.Size(engineWidth, engineHeight)
    }

    private val _selectedResolution = MutableStateFlow(CaptureResolutionPreset.AUTO)
    val selectedResolution: StateFlow<CaptureResolutionPreset> = _selectedResolution.asStateFlow()

    /** 设备实际可用的分辨率列表（排除超出硬件能力的） */
    private val _availableResolutions = MutableStateFlow(listOf(
        CaptureResolutionPreset.AUTO,
        CaptureResolutionPreset.P720,
        CaptureResolutionPreset.P1080
    ))
    val availableResolutions: StateFlow<List<CaptureResolutionPreset>> = _availableResolutions.asStateFlow()

    /** Camera2 枚举的真实摄像头预览支持尺寸（传感器方向） */
    private val _supportedCameraSizes = MutableStateFlow<List<android.util.Size>>(emptyList())
    val supportedCameraSizes: StateFlow<List<android.util.Size>> = _supportedCameraSizes.asStateFlow()

    fun setSupportedCameraSizes(sizes: List<android.util.Size>) {
        _supportedCameraSizes.value = sizes
    }

    fun setResolutionPreset(preset: CaptureResolutionPreset) {
        if (_selectedResolution.value == preset) return
        _selectedResolution.value = preset
    }

    /**
     * 将预设映射到设备真实支持的最接近尺寸（传感器方向）。
     * 若支持列表为空，则退回到预设的默认相机尺寸。
     */
    fun resolveActualCameraSize(preset: CaptureResolutionPreset): android.util.Size {
        val sizes = _supportedCameraSizes.value
        if (sizes.isEmpty()) return preset.toCameraSize()
        return com.sdk.video.capture.CameraResolutionHelper.pickBestSize(sizes, preset.toCameraSize())
            ?: preset.toCameraSize()
    }

    private val _deviceCaps = MutableStateFlow<RenderEngine.DeviceCapabilities?>(null)
    val deviceCaps: StateFlow<RenderEngine.DeviceCapabilities?> = _deviceCaps.asStateFlow()

    fun refreshDeviceCaps() {
        val caps = _filterManager.value?.getDeviceCapabilities()
        _deviceCaps.value = caps
        // 初始默认列表（Camera2 枚举完成后再调用 refreshAvailableResolutionsFromCamera 精确过滤）
        val canHighRes = caps != null && caps.glesVersion >= 31 && caps.fp16
        _availableResolutions.value = buildList {
            add(CaptureResolutionPreset.AUTO)
            add(CaptureResolutionPreset.P720)
            add(CaptureResolutionPreset.P1080)
            if (canHighRes) {
                add(CaptureResolutionPreset.P2K)
                add(CaptureResolutionPreset.P4K)
            }
        }
    }

    /**
     * 根据 Camera2 枚举的真实预览尺寸过滤可用预设。
     * 只保留设备摄像头真正支持的预设（至少有一个接近的匹配尺寸）。
     */
    fun refreshAvailableResolutionsFromCamera(supportedSizes: List<android.util.Size>) {
        if (supportedSizes.isEmpty()) return
        _availableResolutions.value = CaptureResolutionPreset.entries.filter { preset ->
            val mapped = com.sdk.video.capture.CameraResolutionHelper.pickBestSize(
                supportedSizes, preset.toCameraSize()
            )
            mapped != null
        }
    }

    // ── 高级拍摄/录制属性 ──
    private val _speedRate = MutableStateFlow(1f)
    val speedRate: StateFlow<Float> = _speedRate.asStateFlow()

    private val _countdownSeconds = MutableStateFlow(0) // 0=None, 3=3s, 10=10s
    val countdownSeconds: StateFlow<Int> = _countdownSeconds.asStateFlow()

    private val _countdownProgress = MutableStateFlow(-1) // -1=Inactive, 3, 2, 1, 0
    val countdownProgress: StateFlow<Int> = _countdownProgress.asStateFlow()

    private val _maxDurationMs = MutableStateFlow(15000L) // Default 15s (Capture mode)
    val maxDurationMs: StateFlow<Long> = _maxDurationMs.asStateFlow()

    private val _segmentCount = MutableStateFlow(0)
    val segmentCount: StateFlow<Int> = _segmentCount.asStateFlow()

    private val _totalDurationMs = MutableStateFlow(0L)
    val totalDurationMs: StateFlow<Long> = _totalDurationMs.asStateFlow()

    private val _isRecording = MutableStateFlow(false)
    val isRecording: StateFlow<Boolean> = _isRecording.asStateFlow()

    private var startImpl: (() -> Unit)? = null
    private var stopImpl: (() -> Unit)? = null
    private var deleteLastImpl: (() -> Unit)? = null
    private var finalizeImpl: (() -> Unit)? = null
    private var cancelAllImpl: (() -> Unit)? = null

    fun bindRecordingActions(
        start: () -> Unit,
        stop: () -> Unit,
        deleteLast: () -> Unit = {},
        finalize: () -> Unit = {},
        cancelAll: () -> Unit = {}
    ) {
        startImpl = start
        stopImpl = stop
        deleteLastImpl = deleteLast
        finalizeImpl = finalize
        cancelAllImpl = cancelAll
    }

    fun setSpeedRate(rate: Float) {
        _speedRate.value = rate
        _filterManager.value?.setRecordingSpeed(rate)
    }

    fun setCountdownSeconds(seconds: Int) {
        _countdownSeconds.value = seconds
    }

    fun setCountdownProgress(progress: Int) {
        _countdownProgress.value = progress
    }

    fun setMaxDurationMs(duration: Long) {
        _maxDurationMs.value = duration
    }

    fun setSegmentStats(count: Int, totalMs: Long) {
        _segmentCount.value = count
        _totalDurationMs.value = totalMs
    }

    fun deleteLastSegment() {
        deleteLastImpl?.invoke()
    }

    fun finalizeSegments() {
        finalizeImpl?.invoke()
    }

    fun cancelAllSegments() {
        cancelAllImpl?.invoke()
    }

    fun setRecordingState(recording: Boolean) {
        _isRecording.value = recording
    }

    fun toggleRecording() {
        if (_isRecording.value) {
            stopImpl?.invoke()
        } else {
            startImpl?.invoke()
        }
    }

    private val _softwareDecoderAvailable = MutableStateFlow(false)
    val softwareDecoderAvailable: StateFlow<Boolean> = _softwareDecoderAvailable.asStateFlow()

    fun setSoftwareDecoderAvailable(available: Boolean) {
        _softwareDecoderAvailable.value = available
    }

    // ── 捕获会话生命周期回调 ──
    // 由 MainActivity 设置，由 CaptureScreen 通过 DisposableEffect 触发。
    // 解决了"应用启动在首页但 startCamera() 已经调用"的时序死锁：
    //   CameraX SurfaceProvider → awaitInputSurface() → 等 engineState == RUNNING
    //   → 等 FilterCameraPreview.onSurfaceCreated() → 等 CaptureScreen 被组合
    //   → 但默认路由是 "home"，CaptureScreen 根本不存在 → 5 秒超时。
    var onCaptureSessionStart: (() -> Unit)? = null   // MainActivity 设置
    var onCaptureSessionStop: (() -> Unit)? = null     // MainActivity 设置
    var onCameraBindRequested: (() -> Unit)? = null     // 引擎就绪后触发 CameraX 绑定

    /** 由 CaptureScreen DisposableEffect 进入时调用 */
    fun enterCapture() {
        onCaptureSessionStart?.invoke()
    }

    /** 由 CaptureScreen DisposableEffect 离开时调用 */
    fun leaveCapture() {
        onCaptureSessionStop?.invoke()
    }

    /** 由 CaptureScreen 在 filterManager.engineState == RUNNING 时调用 */
    fun requestCameraBind() {
        onCameraBindRequested?.invoke()
    }
}
