package com.sdk.video.capture

import android.util.Log
import androidx.camera.core.Camera
import androidx.camera.core.CameraControl
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * TorchController
 *
 * 手电筒 / 补光灯控制（P2 补齐）。
 *
 * 功能：
 *   - 常亮（Torch）模式：持续补光，适用于夜间录制
 *   - 自动（Auto Flash）模式：由 CameraX ImageCapture 决定
 *   - 关闭
 *   - 支持 CameraX Camera 实例注入，也支持 Camera2 CameraCharacteristics 兼容
 *   - StateFlow 暴露当前状态，UI 可直接 collect
 *
 * 用法：
 *   val torch = TorchController()
 *   torch.bindCamera(camera)           // ProcessCameraProvider 绑定后传入
 *   torch.setMode(TorchController.Mode.TORCH)
 *   // UI: val mode by torch.mode.collectAsState()
 */
class TorchController {

    companion object {
        private const val TAG = "TorchController"
    }

    enum class Mode {
        OFF,    ///< 关闭
        TORCH,  ///< 常亮补光
        AUTO,   ///< 自动（拍照时由 CameraX 决定）
    }

    // ── 状态 ───────────────────────────────────────────────────────────────

    private val _mode = MutableStateFlow(Mode.OFF)
    val mode: StateFlow<Mode> = _mode.asStateFlow()

    private val _hasTorch = MutableStateFlow(false)
    /** 当前相机是否具有手电筒功能。 */
    val hasTorch: StateFlow<Boolean> = _hasTorch.asStateFlow()

    private var camera: Camera? = null

    // ── Camera 绑定 ────────────────────────────────────────────────────────

    /**
     * 绑定 CameraX Camera 实例（在 ProcessCameraProvider.bindToLifecycle 后调用）。
     */
    fun bindCamera(cam: Camera) {
        camera = cam
        val has = cam.cameraInfo.hasFlashUnit()
        _hasTorch.value = has
        Log.d(TAG, "Camera bound — hasFlashUnit=$has")
        // Apply current mode
        applyMode(_mode.value)
    }

    fun unbindCamera() {
        applyMode(Mode.OFF)
        camera = null
        _hasTorch.value = false
    }

    // ── 控制 ───────────────────────────────────────────────────────────────

    /**
     * 设置手电筒模式。
     */
    fun setMode(newMode: Mode) {
        if (_mode.value == newMode) return
        _mode.value = newMode
        applyMode(newMode)
    }

    fun toggle() {
        setMode(if (_mode.value == Mode.TORCH) Mode.OFF else Mode.TORCH)
    }

    private fun applyMode(mode: Mode) {
        val ctrl: CameraControl = camera?.cameraControl ?: return
        when (mode) {
            Mode.TORCH -> {
                ctrl.enableTorch(true)
                Log.d(TAG, "Torch ON")
            }
            Mode.OFF, Mode.AUTO -> {
                ctrl.enableTorch(false)
                Log.d(TAG, "Torch OFF")
            }
        }
    }

    // ── 查询 ───────────────────────────────────────────────────────────────

    fun isTorchOn(): Boolean = _mode.value == Mode.TORCH

    /** 映射到 ImageCapture.FLASH_MODE_* (AUTO=0, ON=1, OFF=2) */
    fun toImageCaptureFlashMode(): Int = when (_mode.value) {
        Mode.AUTO  -> androidx.camera.core.ImageCapture.FLASH_MODE_AUTO
        Mode.TORCH -> androidx.camera.core.ImageCapture.FLASH_MODE_ON
        Mode.OFF   -> androidx.camera.core.ImageCapture.FLASH_MODE_OFF
    }

    fun modeLabel(): String = when (_mode.value) {
        Mode.OFF   -> "关闭"
        Mode.TORCH -> "常亮"
        Mode.AUTO  -> "自动"
    }
}
