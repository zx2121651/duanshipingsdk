package com.sdk.video.sample.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import com.sdk.video.sample.state.ExportState
import com.sdk.video.sample.state.TimelineViewModel
import java.text.SimpleDateFormat
import java.util.*

private val Orange  = Color(0xFFFF6B00)
private val SurfaceDark  = Color(0xFF1A1A1A)
private val SurfaceLight = Color(0xFF252525)
private val TextSec = Color(0xFF888888)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExportScreen(
    navController: NavController,
    timelineVm: TimelineViewModel
) {
    val exportProgress by timelineVm.exportProgress.collectAsState()
    val exportState    by timelineVm.exportState.collectAsState()
    val exportError    by timelineVm.exportError.collectAsState()

    var selectedRes   by remember { mutableStateOf(1) }
    var selectedFps   by remember { mutableStateOf(1) }
    var selectedCodec by remember { mutableStateOf(0) }
    var selectedQuality by remember { mutableStateOf(1) }

    val resOptions     = listOf("480p" to Pair(854, 480), "720p" to Pair(1280, 720), "1080p" to Pair(1920, 1080))
    val fpsOptions     = listOf("24fps", "30fps", "60fps")
    val codecOptions   = listOf("H.264", "H.265")
    val qualityOptions = listOf(
        Triple("流畅", "4 Mbps", 4_000_000),
        Triple("高清", "8 Mbps", 8_000_000),
        Triple("超清", "16 Mbps", 16_000_000)
    )

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("导出设置", color = Color.White, fontWeight = FontWeight.Bold) },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "返回", tint = Color.White)
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF111111))
            )
        },
        containerColor = Color(0xFF0D0D0D)
    ) { padding ->
        when (exportState) {
            ExportState.EXPORTING -> ExportProgressView(
                progress = exportProgress,
                onCancel = {
                    timelineVm.resetExport()
                    navController.popBackStack()
                }
            )

            ExportState.DONE -> ExportSuccessView(
                onEditAgain = {
                    timelineVm.resetExport()
                    navController.popBackStack()
                },
                onBackHome = {
                    timelineVm.resetExport()
                    navController.navigate("home") {
                        popUpTo("home") { inclusive = true }
                    }
                }
            )

            ExportState.ERROR -> ExportErrorView(
                message = exportError ?: "未知错误",
                onRetry = { timelineVm.resetExport() }
            )

            ExportState.IDLE -> {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding)
                        .verticalScroll(rememberScrollState())
                ) {
                    ExportPreviewCard()

                    SettingSection(title = "分辨率") {
                        OptionChipRow(
                            options = resOptions.map { it.first },
                            selectedIndex = selectedRes,
                            onSelect = { selectedRes = it }
                        )
                    }

                    SettingSection(title = "帧率") {
                        OptionChipRow(
                            options = fpsOptions,
                            selectedIndex = selectedFps,
                            onSelect = { selectedFps = it }
                        )
                    }

                    SettingSection(title = "编码格式") {
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            codecOptions.forEachIndexed { i, label ->
                                val isH265 = i == 1
                                OptionChip(
                                    label = if (isH265) "$label（需设备支持）" else label,
                                    selected = selectedCodec == i,
                                    onClick = { selectedCodec = i }
                                )
                            }
                        }
                    }

                    SettingSection(title = "画质预设") {
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            qualityOptions.forEachIndexed { i, (label, sub, _) ->
                                QualityChip(
                                    label = label,
                                    subLabel = sub,
                                    selected = selectedQuality == i,
                                    onClick = { selectedQuality = i }
                                )
                            }
                        }
                    }

                    ExportSummaryCard(
                        res = resOptions[selectedRes].first,
                        fps = fpsOptions[selectedFps],
                        codec = codecOptions[selectedCodec],
                        bitrate = qualityOptions[selectedQuality].second
                    )

                    Spacer(Modifier.height(24.dp))

                    Button(
                        onClick = {
                            val (w, h) = resOptions[selectedRes].second
                            val fps    = listOf(24, 30, 60)[selectedFps]
                            val br     = qualityOptions[selectedQuality].third
                            val ts     = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
                            val path   = "/sdcard/VideoSDK_$ts.mp4"
                            timelineVm.startExport(path, w, h, fps, br)
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 20.dp)
                            .height(52.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = Color.Transparent),
                        contentPadding = PaddingValues(0.dp),
                        shape = RoundedCornerShape(26.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .background(
                                    Brush.horizontalGradient(listOf(Orange, Color(0xFFFF9A3C))),
                                    RoundedCornerShape(26.dp)
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Icon(Icons.Filled.FileDownload, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
                                Text("开始导出", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                            }
                        }
                    }
                    Spacer(Modifier.height(32.dp))
                }
            }
        }
    }
}

@Composable
private fun ExportPreviewCard() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(180.dp)
            .padding(horizontal = 20.dp, vertical = 16.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0xFF1A1A1A)),
        contentAlignment = Alignment.Center
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(Icons.Filled.Movie, contentDescription = null, tint = Color(0xFF444444), modifier = Modifier.size(48.dp))
            Spacer(Modifier.height(8.dp))
            Text("导出预览区域", color = Color(0xFF555555), fontSize = 13.sp)
        }
    }
}

@Composable
private fun SettingSection(title: String, content: @Composable () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 6.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
        content()
        Divider(color = Color(0xFF222222), modifier = Modifier.padding(top = 4.dp))
    }
}

@Composable
private fun OptionChipRow(options: List<String>, selectedIndex: Int, onSelect: (Int) -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        options.forEachIndexed { i, label ->
            OptionChip(label = label, selected = selectedIndex == i, onClick = { onSelect(i) })
        }
    }
}

@Composable
private fun OptionChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (selected) Orange else Color(0xFF1E1E1E))
            .border(1.dp, if (selected) Orange else Color(0xFF333333), RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = Color.White, fontSize = 13.sp, fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal)
    }
}

@Composable
private fun QualityChip(label: String, subLabel: String, selected: Boolean, onClick: () -> Unit) {
    Column(
        modifier = Modifier
            .clip(RoundedCornerShape(10.dp))
            .background(if (selected) Orange.copy(alpha = 0.15f) else Color(0xFF1E1E1E))
            .border(1.5.dp, if (selected) Orange else Color(0xFF333333), RoundedCornerShape(10.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(label, color = if (selected) Orange else Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
        Text(subLabel, color = TextSec, fontSize = 11.sp)
    }
}

@Composable
private fun ExportSummaryCard(res: String, fps: String, codec: String, bitrate: String) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 8.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(SurfaceDark)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Text("导出摘要", color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
        SummaryRow("分辨率", res)
        SummaryRow("帧率", fps)
        SummaryRow("编码", codec)
        SummaryRow("码率", bitrate)
        SummaryRow("输出路径", "/sdcard/VideoSDK_*.mp4")
    }
}

@Composable
private fun SummaryRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, color = TextSec, fontSize = 12.sp)
        Text(value, color = Color.White, fontSize = 12.sp)
    }
}

@Composable
private fun ExportProgressView(progress: Float, onCancel: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Box(contentAlignment = Alignment.Center) {
            CircularProgressIndicator(
                progress = progress,
                modifier = Modifier.size(120.dp),
                color = Orange,
                strokeWidth = 8.dp,
                trackColor = Color(0xFF333333)
            )
            Text(
                text = "${(progress * 100).toInt()}%",
                color = Color.White,
                fontSize = 24.sp,
                fontWeight = FontWeight.Bold
            )
        }
        Spacer(Modifier.height(32.dp))
        Text("正在导出...", color = Color.White, fontSize = 16.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text("请勿退出应用", color = TextSec, fontSize = 13.sp)
        Spacer(Modifier.height(32.dp))
        OutlinedButton(
            onClick = onCancel,
            colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.White),
            border = androidx.compose.foundation.BorderStroke(1.dp, Color(0xFF555555)),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text("取消导出")
        }
    }
}

@Composable
private fun ExportSuccessView(onEditAgain: () -> Unit, onBackHome: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Box(
            modifier = Modifier
                .size(80.dp)
                .clip(androidx.compose.foundation.shape.CircleShape)
                .background(Color(0xFF1A3A1A)),
            contentAlignment = Alignment.Center
        ) {
            Icon(Icons.Filled.CheckCircle, contentDescription = null, tint = Color(0xFF4CAF50), modifier = Modifier.size(56.dp))
        }
        Spacer(Modifier.height(24.dp))
        Text("导出完成！", color = Color.White, fontSize = 22.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text("视频已保存到相册", color = TextSec, fontSize = 14.sp)
        Spacer(Modifier.height(40.dp))
        Button(
            onClick = onEditAgain,
            modifier = Modifier.fillMaxWidth().height(50.dp),
            colors = ButtonDefaults.buttonColors(containerColor = Orange),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text("再次编辑", fontWeight = FontWeight.Bold)
        }
        Spacer(Modifier.height(12.dp))
        OutlinedButton(
            onClick = onBackHome,
            modifier = Modifier.fillMaxWidth().height(50.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.White),
            border = androidx.compose.foundation.BorderStroke(1.dp, Color(0xFF444444)),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text("返回首页")
        }
    }
}

@Composable
private fun ExportErrorView(message: String, onRetry: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxSize().padding(40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(Icons.Filled.Warning, contentDescription = null, tint = Color(0xFFEF4444), modifier = Modifier.size(64.dp))
        Spacer(Modifier.height(16.dp))
        Text("导出失败", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text(message, color = TextSec, fontSize = 13.sp)
        Spacer(Modifier.height(32.dp))
        Button(
            onClick = onRetry,
            colors = ButtonDefaults.buttonColors(containerColor = Orange),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text("重试")
        }
    }
}
