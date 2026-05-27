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

    fun bindFilterManager(fm: VideoFilterManager?) {
        _filterManager.value = fm
    }

    fun bindCamera(camera: Camera?) {
        _camera.value = camera
        if (camera != null) {
            val range = camera.cameraInfo.exposureState.exposureCompensationRange
            _exposureRange.value = IntRange(range.lower, range.upper)
        } else {
            _exposureRange.value = IntRange(0, 0)
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
        val target = !_torchEnabled.value
        _camera.value?.cameraControl?.enableTorch(target)
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
        _beautyEnabled.value = enabled
        val fm = _filterManager.value ?: return
        viewModelScope.launch {
            if (enabled) {
                fm.enableBeauty(_smoothStrength.value, _whitenStrength.value)
            } else {
                fm.disableBeauty()
            }
        }
    }

    fun setSmoothStrength(value: Float) {
        _smoothStrength.value = value
        if (_beautyEnabled.value) {
            val fm = _filterManager.value ?: return
            viewModelScope.launch { fm.enableBeauty(value, _whitenStrength.value) }
        }
    }

    fun setWhitenStrength(value: Float) {
        _whitenStrength.value = value
        if (_beautyEnabled.value) {
            val fm = _filterManager.value ?: return
            viewModelScope.launch { fm.enableBeauty(_smoothStrength.value, value) }
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

        val fm = _filterManager.value ?: return
        viewModelScope.launch {
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

    private val _deviceCaps = MutableStateFlow<RenderEngine.DeviceCapabilities?>(null)
    val deviceCaps: StateFlow<RenderEngine.DeviceCapabilities?> = _deviceCaps.asStateFlow()

    fun refreshDeviceCaps() {
        _deviceCaps.value = _filterManager.value?.getDeviceCapabilities()
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
}
