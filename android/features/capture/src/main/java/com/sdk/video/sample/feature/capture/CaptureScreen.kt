package com.sdk.video.sample.feature.capture

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.runtime.key
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import com.sdk.video.FilterCameraPreview
import com.sdk.video.FilterEngineState
import com.sdk.video.sample.core.model.AppViewModel
import com.sdk.video.sample.core.ui.components.ParameterSlider
import com.sdk.video.sample.core.ui.components.PerfOverlay
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.ui.input.pointer.pointerInput

private val Accent = Color(0xFF00D7FF)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CaptureScreen(viewModel: AppViewModel) {
    val filterManager by viewModel.filterManager.collectAsState()
    val isRecording by viewModel.isRecording.collectAsState()
    val beautyEnabled by viewModel.beautyEnabled.collectAsState()
    val smooth by viewModel.smoothStrength.collectAsState()
    val whiten by viewModel.whitenStrength.collectAsState()
    val eye by viewModel.eyeScale.collectAsState()
    val faceSlim by viewModel.faceSlim.collectAsState()
    val noseSlim by viewModel.noseSlim.collectAsState()
    val chinV by viewModel.chinV.collectAsState()

    val zoomRatio by viewModel.zoomRatio.collectAsState()
    val exposureIndex by viewModel.exposureIndex.collectAsState()
    val exposureRange by viewModel.exposureRange.collectAsState()
    val torchEnabled by viewModel.torchEnabled.collectAsState()

    val totalDurationMs by viewModel.totalDurationMs.collectAsState()
    val maxDurationMs by viewModel.maxDurationMs.collectAsState()
    val segmentCount by viewModel.segmentCount.collectAsState()
    val countdownProgress by viewModel.countdownProgress.collectAsState()
    val countdownSeconds by viewModel.countdownSeconds.collectAsState()
    val speedRate by viewModel.speedRate.collectAsState()

    var showBeautyPanel by remember { mutableStateOf(false) }
    var showSpeedPanel by remember { mutableStateOf(false) }
    var showCountdownPanel by remember { mutableStateOf(false) }
    var showExposureSlider by remember { mutableStateOf(false) }

    // ── 捕获会话生命周期 ──────────────────────────────────────────────────
    // 只在此页面存在时才启动相机管线，离开时解绑。
    // 这解决了"应用启动在首页但 startCamera() 已调用 → CameraX 5 秒超时"的时序死锁。
    DisposableEffect(Unit) {
        viewModel.enterCapture()
        onDispose { viewModel.leaveCapture() }
    }

    // 引擎就绪后触发 CameraX 绑定（bindToLifecycle）
    val engineState by remember(filterManager) {
        filterManager?.engineState ?: kotlinx.coroutines.flow.MutableStateFlow(FilterEngineState.STOPPED)
    }.collectAsState()

    LaunchedEffect(engineState) {
        if (engineState == FilterEngineState.RUNNING) {
            viewModel.requestCameraBind()
        }
    }

    Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {

        // 1. 底层 OpenGL ES 相机预览 Surface 视图 (加 Pinch 手势变焦)
        filterManager?.let { fm ->
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .pointerInput(zoomRatio) {
                        detectTransformGestures { _, _, zoom, _ ->
                            val newZoom = (zoomRatio * zoom).coerceIn(1f, 8f)
                            viewModel.setZoomRatio(newZoom)
                        }
                    }
            ) {
                key(fm) {
                    FilterCameraPreview(
                        filterManager = fm,
                        modifier = Modifier.fillMaxSize()
                    )
                }
            }
        } ?: Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("正在初始化实时预览...", color = Color.White)
        }

        // 顶部进度条 (分段拍进度渲染)
        if (maxDurationMs > 0L) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 40.dp, start = 16.dp, end = 16.dp)
                    .height(6.dp)
                    .clip(RoundedCornerShape(3.dp))
                    .background(Color(0x44FFFFFF))
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxHeight()
                        .fillMaxWidth(fraction = (totalDurationMs.toFloat() / maxDurationMs).coerceIn(0f, 1f))
                        .background(Accent)
                )
            }
        }

        // 2. 实时性能调试指标 overlay
        filterManager?.let { fm ->
            PerfOverlay(
                filterManager = fm,
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(start = 12.dp, top = 56.dp)
            )
        }

        // 3. 右侧快捷录制工具按钮面板
        Column(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = 56.dp, end = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            CaptureToolButton(
                icon = Icons.Filled.Cameraswitch,
                label = "翻转",
                enabled = !isRecording
            ) {
                viewModel.toggleCameraFacing()
            }
            CaptureToolButton(Icons.Filled.Face, "美颜") { showBeautyPanel = true }
            CaptureToolButton(Icons.Filled.Speed, "速度") {
                showSpeedPanel = !showSpeedPanel
                showCountdownPanel = false
                showExposureSlider = false
            }
            CaptureToolButton(Icons.Filled.Timer, "倒计时") {
                showCountdownPanel = !showCountdownPanel
                showSpeedPanel = false
                showExposureSlider = false
            }
            CaptureToolButton(Icons.Filled.Exposure, "曝光") {
                showExposureSlider = !showExposureSlider
                showSpeedPanel = false
                showCountdownPanel = false
            }
            CaptureToolButton(
                icon = if (torchEnabled) Icons.Filled.FlashOn else Icons.Filled.FlashOff,
                label = "补光"
            ) {
                viewModel.toggleTorch()
            }
        }

        // 曝光滑块横放于快门上方
        if (showExposureSlider && exposureRange.first < exposureRange.last) {
            Column(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(bottom = 125.dp)
                    .background(Color(0x99000000), RoundedCornerShape(12.dp))
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text("曝光值调节: ${if (exposureIndex > 0) "+" else ""}$exposureIndex", color = Color.White, fontSize = 12.sp)
                Slider(
                    value = exposureIndex.toFloat(),
                    onValueChange = { viewModel.setExposureIndex(it.toInt()) },
                    valueRange = exposureRange.first.toFloat()..exposureRange.last.toFloat(),
                    steps = exposureRange.last - exposureRange.first - 1,
                    colors = SliderDefaults.colors(
                        thumbColor = Accent,
                        activeTrackColor = Accent,
                        inactiveTrackColor = Color.Gray
                    ),
                    modifier = Modifier.width(220.dp)
                )
            }
        }

        // 速度面板
        if (showSpeedPanel) {
            Row(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(bottom = 125.dp)
                    .background(Color(0x99000000), RoundedCornerShape(20.dp))
                    .padding(horizontal = 16.dp, vertical = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                listOf(0.3f, 0.5f, 1f, 2f, 3f).forEach { rate ->
                    Text(
                        text = "${rate}x",
                        color = if (speedRate == rate) Accent else Color.White,
                        fontWeight = if (speedRate == rate) FontWeight.Bold else FontWeight.Normal,
                        fontSize = 14.sp,
                        modifier = Modifier
                            .clickable { viewModel.setSpeedRate(rate) }
                            .padding(horizontal = 8.dp, vertical = 4.dp)
                    )
                }
            }
        }

        // 倒计时配置面板
        if (showCountdownPanel) {
            Row(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(bottom = 125.dp)
                    .background(Color(0x99000000), RoundedCornerShape(20.dp))
                    .padding(horizontal = 16.dp, vertical = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("倒计时", color = Color.Gray, fontSize = 12.sp)
                listOf(0 to "关闭", 3 to "3秒", 10 to "10秒").forEach { (secs, label) ->
                    Text(
                        text = label,
                        color = if (countdownSeconds == secs) Accent else Color.White,
                        fontWeight = if (countdownSeconds == secs) FontWeight.Bold else FontWeight.Normal,
                        fontSize = 14.sp,
                        modifier = Modifier
                            .clickable { viewModel.setCountdownSeconds(secs) }
                            .padding(horizontal = 8.dp, vertical = 4.dp)
                    )
                }
            }
        }

        // 倒计时大字提示
        if (countdownProgress >= 0) {
            Box(
                modifier = Modifier.fillMaxSize().background(Color(0x44000000)),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = if (countdownProgress == 0) "GO!" else countdownProgress.toString(),
                    color = Accent,
                    fontSize = 120.sp,
                    fontWeight = FontWeight.Black
                )
            }
        }

        // 4. 底部控制栏：包含拍摄模式切换与录制快门按钮
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .background(Color(0x66000000))
                .padding(bottom = 24.dp, top = 12.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            // 模式选择轨 (2分钟 / 30秒 / 15秒 / 照片)
            Row(horizontalArrangement = Arrangement.spacedBy(22.dp)) {
                listOf(
                    120000L to "2分钟",
                    30000L to "30秒",
                    15000L to "15秒",
                    0L to "照片"
                ).forEach { (duration, label) ->
                    val isSelected = maxDurationMs == duration
                    Text(
                        text = label,
                        color = if (isSelected) Accent else Color(0xFFBBBBBB),
                        fontSize = if (isSelected) 15.sp else 13.sp,
                        fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                        modifier = Modifier.clickable {
                            if (!isRecording && segmentCount == 0) {
                                viewModel.setMaxDurationMs(duration)
                            }
                        }
                    )
                }
            }
            Spacer(Modifier.height(14.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // 如果录制了片段且未处于录制中：左边变"回删"；右边变"完成"
                if (segmentCount > 0 && !isRecording) {
                    MiniCaptureAction("回删", Icons.Filled.Undo) {
                        viewModel.deleteLastSegment()
                    }
                    RecordButton(isRecording = isRecording) { viewModel.toggleRecording() }
                    MiniCaptureAction("完成", Icons.Filled.Check) {
                        viewModel.finalizeSegments()
                    }
                } else {
                    MiniCaptureAction("特效", Icons.Filled.AutoFixHigh) { showBeautyPanel = true }
                    RecordButton(isRecording = isRecording) { viewModel.toggleRecording() }
                    MiniCaptureAction("美颜", Icons.Filled.Face) { showBeautyPanel = true }
                }
            }
        }

        // 5. 底部美颜美型详细调节面板
        if (showBeautyPanel) {
            ModalBottomSheet(
                onDismissRequest = { showBeautyPanel = false },
                containerColor = Color(0xFF151515)
            ) {
                Column(modifier = Modifier.padding(bottom = 24.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("美颜美型", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                            Text("磨皮、美白、大眼、瘦脸、瘦鼻、V 下巴", color = Color(0xFF999999), fontSize = 12.sp)
                        }
                        Switch(
                            checked = beautyEnabled,
                            onCheckedChange = { viewModel.setBeautyEnabled(it) }
                        )
                    }

                    if (beautyEnabled) {
                        ParameterSlider("磨皮", smooth, { viewModel.setSmoothStrength(it) })
                        ParameterSlider("美白", whiten, { viewModel.setWhitenStrength(it) })
                    }

                    ParameterSlider("大眼", eye, { viewModel.setReshape(eye = it) }, 0f..0.5f)
                    ParameterSlider("瘦脸", faceSlim, { viewModel.setReshape(slim = it) }, 0f..0.4f)
                    ParameterSlider("瘦鼻", noseSlim, { viewModel.setReshape(nose = it) }, 0f..0.4f)
                    ParameterSlider("V 脸", chinV, { viewModel.setReshape(chin = it) }, 0f..0.4f)
                }
            }
        }
    }
}

@Composable
private fun CaptureToolButton(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    enabled: Boolean = true,
    onClick: () -> Unit
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(3.dp)) {
        IconButton(
            onClick = onClick,
            enabled = enabled,
            modifier = Modifier
                .clip(CircleShape)
                .background(if (enabled) Color(0x77000000) else Color(0x22000000))
                .size(44.dp)
        ) {
            Icon(
                icon,
                contentDescription = label,
                tint = if (enabled) Color.White else Color.Gray,
                modifier = Modifier.size(22.dp)
            )
        }
        Text(label, color = if (enabled) Color.White else Color.Gray, fontSize = 10.sp)
    }
}

@Composable
private fun RecordButton(isRecording: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .size(78.dp)
            .clip(CircleShape)
            .background(Color.White)
            .padding(8.dp),
        contentAlignment = Alignment.Center
    ) {
        Button(
            onClick = onClick,
            colors = ButtonDefaults.buttonColors(
                containerColor = if (isRecording) Color(0xFF333333) else Color(0xFFEF4444)
            ),
            shape = CircleShape,
            contentPadding = PaddingValues(0.dp),
            modifier = Modifier.fillMaxSize()
        ) {
            if (isRecording) {
                Box(
                    modifier = Modifier
                        .size(24.dp)
                        .clip(RoundedCornerShape(5.dp))
                        .background(Color.White)
                )
            }
        }
    }
}

@Composable
private fun MiniCaptureAction(
    label: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    onClick: () -> Unit
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.width(70.dp)) {
        Box(
            modifier = Modifier
                .size(42.dp)
                .clip(RoundedCornerShape(12.dp))
                .background(Accent.copy(alpha = 0.18f))
                .clickable(onClick = onClick),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, contentDescription = label, tint = Accent, modifier = Modifier.size(22.dp))
        }
        Spacer(Modifier.height(5.dp))
        Text(label, color = Color.White, fontSize = 11.sp)
    }
}
