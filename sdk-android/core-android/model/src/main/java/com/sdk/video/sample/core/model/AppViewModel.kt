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

/**
 * 全局共享核心业务状态 (AppViewModel)
 *
 * 负责录制、滤镜参数、特效应用、设备硬件参数等系统级状态的分发与 native 层同步：
 * - 声明与驱动来自底层的 [VideoFilterManager] 控制接口。
 * - 管理多路 UI 状态参数（如美颜、瘦脸、滤镜列表、当前激活特效），缓存状态防止在 Compose Tab 页面切换时丢失。
 * - 向上解耦各 Feature 模块（如拍摄、特效、诊断监控）的逻辑，通过 Flows 统一分发状态。
 */
class AppViewModel : ViewModel() {

    // ── SDK 接口代理同步器 ───────────────────────────────────────────────
    private val _filterManager = MutableStateFlow<VideoFilterManager?>(null)
    val filterManager: StateFlow<VideoFilterManager?> = _filterManager.asStateFlow()

    /**
     * 绑定底层的 VideoFilterManager 实例
     * 通常在 MainActivity 中当 CameraX 协商分辨率完成后进行注入
     */
    fun bindFilterManager(fm: VideoFilterManager?) {
        _filterManager.value = fm
    }

    // ── 滤镜状态管线 ───────────────────────────────────────────────
    private val _activeFilters = MutableStateFlow<List<VideoFilterType>>(emptyList())
    val activeFilters: StateFlow<List<VideoFilterType>> = _activeFilters.asStateFlow()

    /**
     * 切换特定滤镜的状态。
     * 若已包含则进行移除；否则加入链条，并触发底层重新调度 EGL 滤镜链。
     */
    fun toggleFilter(type: VideoFilterType) {
        val fm = _filterManager.value ?: return
        const val current = _activeFilters.value
        val newList = if (current.contains(type)) current - type else current + type
        _activeFilters.value = newList

        viewModelScope.launch {
            // 先清理管线中所有旧滤镜，再依序重新追加，以保证滤镜叠加顺序的正确性。
            // 美颜和人脸抠像特效通过 NativeBridge::rebuildAiPipeline 自动保留恢复。
            fm.removeAllFilters()
            for (f in newList) fm.addFilter(f)
        }
    }

    // ── 智能美颜状态（参数联动） ──────────────────────────────────────
    private val _beautyEnabled = MutableStateFlow(false)
    val beautyEnabled: StateFlow<Boolean> = _beautyEnabled.asStateFlow()

    private val _smoothStrength = MutableStateFlow(0.6f) // 磨皮强度，默认 0.6f
    val smoothStrength: StateFlow<Float> = _smoothStrength.asStateFlow()

    private val _whitenStrength = MutableStateFlow(0.4f) // 美白强度，默认 0.4f
    val whitenStrength: StateFlow<Float> = _whitenStrength.asStateFlow()

    /**
     * 开启/关闭美颜
     */
    fun setBeautyEnabled(enabled: Boolean) {
        _beautyEnabled.value = enabled
        val fm = _filterManager.value ?: return
        viewModelScope.launch {
            if (enabled) fm.enableBeauty(_smoothStrength.value, _whitenStrength.value)
            else fm.disableBeauty()
        }
    }

    /**
     * 更新磨皮强度
     */
    fun setSmoothStrength(v: Float) {
        _smoothStrength.value = v
        if (_beautyEnabled.value) {
            val fm = _filterManager.value ?: return
            viewModelScope.launch { fm.enableBeauty(v, _whitenStrength.value) }
        }
    }

    /**
     * 更新美白强度
     */
    fun setWhitenStrength(v: Float) {
        _whitenStrength.value = v
        if (_beautyEnabled.value) {
            val fm = _filterManager.value ?: return
            viewModelScope.launch { fm.enableBeauty(_smoothStrength.value, v) }
        }
    }

    // ── 人脸重塑与大眼瘦脸状态 ──────────────────────────────────────
    private val _eyeScale  = MutableStateFlow(0f)
    private val _faceSlim  = MutableStateFlow(0f)
    private val _noseSlim  = MutableStateFlow(0f)
    private val _chinV     = MutableStateFlow(0f)
    
    val eyeScale:  StateFlow<Float> = _eyeScale.asStateFlow()
    val faceSlim:  StateFlow<Float> = _faceSlim.asStateFlow()
    val noseSlim:  StateFlow<Float> = _noseSlim.asStateFlow()
    val chinV:     StateFlow<Float> = _chinV.asStateFlow()

    /**
     * 设置脸部细节重塑参数，并将变动刷新到底层 native 的 C++ 人脸特征点调整矩阵中
     */
    fun setReshape(
        eye: Float = _eyeScale.value,
        slim: Float = _faceSlim.value,
        nose: Float = _noseSlim.value,
        chin: Float = _chinV.value
    ) {
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

    // ── 智能特效包管理 ─────────────────────────────────────────────
    private val _activeEffectId = MutableStateFlow<String?>(null)
    val activeEffectId: StateFlow<String?> = _activeEffectId.asStateFlow()

    private val loadedEffectIds = mutableSetOf<String>()

    /**
     * 直接激活已解析过的本地特效包
     */
    fun activateEffect(id: String?) {
        _activeEffectId.value = id
        val fm = _filterManager.value ?: return
        viewModelScope.launch {
            if (id.isNullOrEmpty()) fm.deactivateAllEffects()
            else fm.activateEffect(id)
        }
    }

    /**
     * 首次载入并激活某特效包 (解析 assets 文件夹下的 json 并初始化纹理 FBO)
     * 
     * @param effectRoot 特效资源在设备上的绝对物理路径
     * @param expectedId 预期的特效 id
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

    // ── 图像微调滤镜参数 map (亮度、饱和度等) ──────────────────────────────────
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

    // ── 相机朝向状态 ────────────────────────────────────────────
    private val _useFrontCamera = MutableStateFlow(true)
    val useFrontCamera: StateFlow<Boolean> = _useFrontCamera.asStateFlow()

    fun toggleCameraFacing() {
        _useFrontCamera.value = !_useFrontCamera.value
    }

    // ── 设备 GL 硬件能力快照 ─────────────────────────────────────────
    private val _deviceCaps = MutableStateFlow<RenderEngine.DeviceCapabilities?>(null)
    val deviceCaps: StateFlow<RenderEngine.DeviceCapabilities?> = _deviceCaps.asStateFlow()

    fun refreshDeviceCaps() {
        _deviceCaps.value = _filterManager.value?.getDeviceCapabilities()
    }

    // ── 录制状态与生命周期驱动（由 Activity 实现回调绑定） ───────────────────
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

    // ── 软解解码器可用性标识 (FFmpeg JNI 绑定状态) ───────────────────────────
    private val _softwareDecoderAvailable = MutableStateFlow(false)
    val softwareDecoderAvailable: StateFlow<Boolean> = _softwareDecoderAvailable.asStateFlow()

    fun setSoftwareDecoderAvailable(available: Boolean) {
        _softwareDecoderAvailable.value = available
    }
}
