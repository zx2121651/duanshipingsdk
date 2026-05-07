package com.sdk.video.sample.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.Cameraswitch
import androidx.compose.material.icons.filled.Face
import androidx.compose.material.icons.filled.FlashOn
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
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

    var showBeautyPanel by remember { mutableStateOf(false) }
    var selectedMode by remember { mutableStateOf("拍摄") }

    Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {
        filterManager?.let { fm ->
            FilterCameraPreview(
                filterManager = fm,
                modifier = Modifier.fillMaxSize()
            )
        } ?: Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("正在初始化实时预览...", color = Color.White)
        }

        filterManager?.let { fm ->
            PerfOverlay(
                filterManager = fm,
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(start = 12.dp, top = 12.dp)
            )
        }

        Column(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = 18.dp, end = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            CaptureToolButton(Icons.Filled.Cameraswitch, "翻转") { viewModel.toggleCameraFacing() }
            CaptureToolButton(Icons.Filled.Face, "美颜") { showBeautyPanel = true }
            CaptureToolButton(Icons.Filled.AutoFixHigh, "特效") { showBeautyPanel = true }
            CaptureToolButton(Icons.Filled.MusicNote, "音乐") {}
            CaptureToolButton(Icons.Filled.Speed, "速度") {}
            CaptureToolButton(Icons.Filled.FlashOn, "闪光") {}
        }

        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .background(Color(0x66000000))
                .padding(bottom = 24.dp, top = 12.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(22.dp)) {
                listOf("模板", "拍摄", "照片").forEach { mode ->
                    Text(
                        text = mode,
                        color = if (selectedMode == mode) Color.White else Color(0xFFBBBBBB),
                        fontSize = if (selectedMode == mode) 15.sp else 13.sp,
                        fontWeight = if (selectedMode == mode) FontWeight.Bold else FontWeight.Normal,
                        modifier = Modifier.clickable { selectedMode = mode }
                    )
                }
            }
            Spacer(Modifier.height(14.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                MiniCaptureAction("特效", Icons.Filled.AutoFixHigh) { showBeautyPanel = true }
                RecordButton(isRecording = isRecording) { viewModel.toggleRecording() }
                MiniCaptureAction("美颜", Icons.Filled.Face) { showBeautyPanel = true }
            }
        }

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
                            Text("对标短视频 SDK：磨皮、美白、大眼、瘦脸、瘦鼻、V 下巴", color = Color(0xFF999999), fontSize = 12.sp)
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
    onClick: () -> Unit
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(3.dp)) {
        IconButton(
            onClick = onClick,
            modifier = Modifier
                .clip(CircleShape)
                .background(Color(0x77000000))
                .size(44.dp)
        ) {
            Icon(icon, contentDescription = label, tint = Color.White, modifier = Modifier.size(22.dp))
        }
        Text(label, color = Color.White, fontSize = 10.sp)
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
            contentPadding = androidx.compose.foundation.layout.PaddingValues(0.dp),
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
