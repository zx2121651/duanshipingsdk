@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.sample.state

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.sdk.video.RenderEngine
import com.sdk.video.VideoFilterManager
import com.sdk.video.VideoFilterType
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * 全谱 Demo 共享状态。
 *
 * MainActivity 在 CameraX 协商分辨率后构造 [VideoFilterManager]，
 * 通过 [bindFilterManager] 注入；之后所有 Compose 屏幕通过 [filterManager]
 * 状态流订阅最新实例。
 *
 * UI 状态字段（启用滤镜 / 当前特效 / 美颜参数等）保留在 ViewModel，
 * 便于跨 Tab 切换不丢失用户选择。
 */
class AppViewModel : ViewModel() {

    // ── SDK Facade ───────────────────────────────────────────────
    private val _filterManager = MutableStateFlow<VideoFilterManager?>(null)
    val filterManager: StateFlow<VideoFilterManager?> = _filterManager.asStateFlow()

    fun bindFilterManager(fm: VideoFilterManager?) {
        _filterManager.value = fm
    }

    // ── UI 状态：当前激活的滤镜列表（编辑页用） ────────────────
    private val _activeFilters = MutableStateFlow<List<VideoFilterType>>(emptyList())
    val activeFilters: StateFlow<List<VideoFilterType>> = _activeFilters.asStateFlow()

    /** 切换某个滤镜：未启用则添加，已启用则移除并重建管线。 */
    fun toggleFilter(type: VideoFilterType) {
        val fm = _filterManager.value ?: return
        val current = _activeFilters.value
        val newList = if (current.contains(type)) current - type else current + type
        _activeFilters.value = newList

        viewModelScope.launch {
            // Simplest: clear all then re-add in order. Beauty / AI filters are
            // restored automatically by NativeBridge::rebuildAiPipeline.
            fm.removeAllFilters()
            for (f in newList) fm.addFilter(f)
        }
    }

    // ── UI 状态：美颜参数 ──────────────────────────────────────
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
            if (enabled) fm.enableBeauty(_smoothStrength.value, _whitenStrength.value)
            else fm.disableBeauty()
        }
    }

    fun setSmoothStrength(v: Float) {
        _smoothStrength.value = v
        if (_beautyEnabled.value) {
            val fm = _filterManager.value ?: return
            viewModelScope.launch { fm.enableBeauty(v, _whitenStrength.value) }
        }
    }

    fun setWhitenStrength(v: Float) {
        _whitenStrength.value = v
        if (_beautyEnabled.value) {
            val fm = _filterManager.value ?: return
            viewModelScope.launch { fm.enableBeauty(_smoothStrength.value, v) }
        }
    }

    // ── UI 状态：人脸重塑 ──────────────────────────────────────
    private val _eyeScale  = MutableStateFlow(0f)
    private val _faceSlim  = MutableStateFlow(0f)
    private val _noseSlim  = MutableStateFlow(0f)
    private val _chinV     = MutableStateFlow(0f)
    val eyeScale:  StateFlow<Float> = _eyeScale.asStateFlow()
    val faceSlim:  StateFlow<Float> = _faceSlim.asStateFlow()
    val noseSlim:  StateFlow<Float> = _noseSlim.asStateFlow()
    val chinV:     StateFlow<Float> = _chinV.asStateFlow()

    fun setReshape(eye: Float = _eyeScale.value,
                   slim: Float = _faceSlim.value,
                   nose: Float = _noseSlim.value,
                   chin: Float = _chinV.value) {
        _eyeScale.value = eye
        _faceSlim.value = slim
        _noseSlim.value = nose
        _chinV.value    = chin
        val fm = _filterManager.value ?: return
        viewModelScope.launch {
            fm.setFaceReshape(
                eyeScale = eye,
                faceSlim = slim,
                noseSlim = nose,
                chinV    = chin
            )
        }
    }

    // ── UI 状态：当前激活特效包 ID ─────────────────────────────
    private val _activeEffectId = MutableStateFlow<String?>(null)
    val activeEffectId: StateFlow<String?> = _activeEffectId.asStateFlow()

    private val loadedEffectIds = mutableSetOf<String>()

    fun activateEffect(id: String?) {
        _activeEffectId.value = id
        val fm = _filterManager.value ?: return
        viewModelScope.launch {
            if (id.isNullOrEmpty()) fm.deactivateAllEffects()
            else fm.activateEffect(id)
        }
    }

    /**
     * 首次激活时调用：加载特效包（解析 manifest.json）+ 立即激活。
     * 同一 id 重复调用只走 activateEffect 快路径。
     *
     * @param effectRoot 特效目录绝对路径（filesDir/effects/<id>）
     * @param expectedId manifest.json 中预期的 id；若 loadEffect 返回不一致会抛错日志
     */
    fun loadAndActivateEffect(effectRoot: String, expectedId: String) {
        val fm = _filterManager.value ?: return
        viewModelScope.launch {
            if (!loadedEffectIds.contains(expectedId)) {
                val res = fm.loadEffect(effectRoot)
                val id = res.getOrNull() ?: ""
                if (id.isNotEmpty()) loadedEffectIds.add(id)
            }
            fm.activateEffect(expectedId)
            _activeEffectId.value = expectedId
        }
    }

    // ── UI 状态：滤镜参数 (key -> float)，编辑页 slider 共享 ───
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

    // ── 摄像头朝向 ────────────────────────────────────────────
    private val _useFrontCamera = MutableStateFlow(true)
    val useFrontCamera: StateFlow<Boolean> = _useFrontCamera.asStateFlow()

    fun toggleCameraFacing() {
        _useFrontCamera.value = !_useFrontCamera.value
    }

    // ── 设备能力快照（仅在引擎 ready 后第一次拉取后缓存） ────
    private val _deviceCaps = MutableStateFlow<RenderEngine.DeviceCapabilities?>(null)
    val deviceCaps: StateFlow<RenderEngine.DeviceCapabilities?> = _deviceCaps.asStateFlow()

    fun refreshDeviceCaps() {
        _deviceCaps.value = _filterManager.value?.getDeviceCapabilities()
    }

    // ── 录制状态 + 操作（由 MainActivity 注入实现，UI 触发） ─
    private val _isRecording = MutableStateFlow(false)
    val isRecording: StateFlow<Boolean> = _isRecording.asStateFlow()

    private var startImpl: (() -> Unit)? = null
    private var stopImpl:  (() -> Unit)? = null

    fun bindRecordingActions(start: () -> Unit, stop: () -> Unit) {
        startImpl = start
        stopImpl  = stop
    }

    fun setRecordingState(recording: Boolean) {
        _isRecording.value = recording
    }

    fun toggleRecording() {
        if (_isRecording.value) stopImpl?.invoke()
        else startImpl?.invoke()
    }

    // ── 软解可用性（由 MainActivity 注入，诊断页消费） ──────
    private val _softwareDecoderAvailable = MutableStateFlow(false)
    val softwareDecoderAvailable: StateFlow<Boolean> = _softwareDecoderAvailable.asStateFlow()

    fun setSoftwareDecoderAvailable(available: Boolean) {
        _softwareDecoderAvailable.value = available
    }
}
