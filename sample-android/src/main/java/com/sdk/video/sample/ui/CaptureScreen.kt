package com.sdk.video.sample.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Cameraswitch
import androidx.compose.material.icons.filled.Face
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sdk.video.FilterCameraPreview
import com.sdk.video.sample.state.AppViewModel
import com.sdk.video.sample.ui.components.ParameterSlider
import com.sdk.video.sample.ui.components.PerfOverlay

/**
 * 拍摄页：摄像头预览 + 录制按钮 + 美颜快捷面板 + 摄像头切换。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CaptureScreen(viewModel: AppViewModel) {
    val filterManager by viewModel.filterManager.collectAsState()
    val isRecording   by viewModel.isRecording.collectAsState()
    val beautyEnabled by viewModel.beautyEnabled.collectAsState()
    val smooth        by viewModel.smoothStrength.collectAsState()
    val whiten        by viewModel.whitenStrength.collectAsState()
    val eye           by viewModel.eyeScale.collectAsState()
    val faceSlim      by viewModel.faceSlim.collectAsState()
    val noseSlim      by viewModel.noseSlim.collectAsState()
    val chinV         by viewModel.chinV.collectAsState()

    var showBeautyPanel by remember { mutableStateOf(false) }

    Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {

        // ── Background: live camera preview ─────────────────────
        filterManager?.let { fm ->
            FilterCameraPreview(
                filterManager = fm,
                modifier = Modifier.fillMaxSize()
            )
        } ?: Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("Initializing engine…", color = Color.White)
        }

        // ── Top-left: performance overlay ───────────────────────
        filterManager?.let { fm ->
            PerfOverlay(
                filterManager = fm,
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(start = 12.dp, top = 12.dp)
            )
        }

        // ── Top-right: camera switch ────────────────────────────
        IconButton(
            onClick = { viewModel.toggleCameraFacing() },
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = 12.dp, end = 12.dp)
                .clip(CircleShape)
                .background(Color(0x88000000))
                .size(44.dp)
        ) {
            Icon(Icons.Filled.Cameraswitch, contentDescription = "Switch camera", tint = Color.White)
        }

        // ── Bottom-left: beauty toggle ──────────────────────────
        IconButton(
            onClick = { showBeautyPanel = true },
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(start = 24.dp, bottom = 32.dp)
                .clip(CircleShape)
                .background(if (beautyEnabled) Color(0xCCFF66B2) else Color(0x88000000))
                .size(56.dp)
        ) {
            Icon(Icons.Filled.Face, contentDescription = "Beauty", tint = Color.White)
        }

        // ── Bottom-center: record button ────────────────────────
        Box(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = 28.dp)
                .size(76.dp)
                .clip(CircleShape)
                .background(Color(0xFFFFFFFF))
                .padding(8.dp),
            contentAlignment = Alignment.Center
        ) {
            Button(
                onClick = { viewModel.toggleRecording() },
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (isRecording) Color(0xFF333333) else Color(0xFFEF4444)
                ),
                shape = CircleShape,
                contentPadding = PaddingValues(0.dp),
                modifier = Modifier.fillMaxSize()
            ) { /* empty content — just the colored circle */ }
        }

        // ── Beauty panel modal sheet ────────────────────────────
        if (showBeautyPanel) {
            ModalBottomSheet(
                onDismissRequest = { showBeautyPanel = false },
                containerColor = Color(0xFF1A1A1A)
            ) {
                Column(modifier = Modifier.padding(bottom = 24.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("美颜", color = Color.White, fontSize = 18.sp,
                             fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                        Switch(
                            checked = beautyEnabled,
                            onCheckedChange = { viewModel.setBeautyEnabled(it) }
                        )
                    }

                    if (beautyEnabled) {
                        ParameterSlider("磨皮", smooth, { viewModel.setSmoothStrength(it) })
                        ParameterSlider("美白", whiten, { viewModel.setWhitenStrength(it) })
                    }

                    Spacer(Modifier.height(8.dp))
                    Text("人脸重塑（需先加载 landmark 模型）", color = Color(0xAAAAAAAA),
                         fontSize = 12.sp, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))

                    ParameterSlider("大眼",  eye,
                        { viewModel.setReshape(eye = it) }, 0f..0.5f)
                    ParameterSlider("瘦脸",  faceSlim,
                        { viewModel.setReshape(slim = it) }, 0f..0.4f)
                    ParameterSlider("瘦鼻",  noseSlim,
                        { viewModel.setReshape(nose = it) }, 0f..0.4f)
                    ParameterSlider("V下巴", chinV,
                        { viewModel.setReshape(chin = it) }, 0f..0.4f)
                }
            }
        }
    }
}
