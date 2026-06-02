package com.sdk.video.sample.feature.export

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Divider
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import com.sdk.video.sample.core.model.ExportState
import com.sdk.video.sample.core.model.TimelineViewModel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private val Accent = Color(0xFF00D7FF)
private val SurfaceDark = Color(0xFF1A1A1A)
private val TextSec = Color(0xFF888888)

/**
 * 视频导出配置与进度展示页面 (ExportScreen)
 *
 * 核心逻辑与功能:
 * 1. 导出属性配置：提供分辨率选择（720p - 4K）、帧率设置（24fps - 60fps）、视频编码格式（H.264/H.265）以及输出码率档位。
 * 2. 导出摘要渲染：根据用户选择的属性动态渲染当前的导出摘要，直观呈现视频参数。
 * 3. 异步导出驱动：点击“开始导出”，根据当前时间戳组装模拟输出路径，调用 [TimelineViewModel.startExport] 进行多帧编码合并。
 * 4. 多状态视图流转换：使用状态收集机制监听 [ExportState]，在 IDLE、EXPORTING、DONE 和 ERROR 视图间平滑切换，提供良好的交互反馈。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExportScreen(
    navController: NavController,
    timelineVm: TimelineViewModel
) {
    val exportProgress by timelineVm.exportProgress.collectAsState()
    val exportState by timelineVm.exportState.collectAsState()
    val exportError by timelineVm.exportError.collectAsState()

    var selectedRes by remember { mutableStateOf(2) }
    var selectedFps by remember { mutableStateOf(1) }
    var selectedCodec by remember { mutableStateOf(0) }
    var selectedQuality by remember { mutableStateOf(1) }

    val resOptions = listOf("720p" to Pair(1280, 720), "1080p" to Pair(1920, 1080), "2K" to Pair(2560, 1440), "4K" to Pair(3840, 2160))
    val fpsOptions = listOf("24fps", "30fps", "60fps")
    val codecOptions = listOf("H.264", "H.265")
    val qualityOptions = listOf(
        Triple("标准", "6 Mbps", 6_000_000),
        Triple("高清", "12 Mbps", 12_000_000),
        Triple("超清", "24 Mbps", 24_000_000)
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
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF101010))
            )
        },
        containerColor = Color(0xFF0B0B0C)
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
                message = exportError ?: "导出失败，请检查编码器或输出路径。",
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
                            codecOptions.forEachIndexed { index, label ->
                                OptionChip(
                                    label = if (index == 1) "$label · 高压缩" else label,
                                    selected = selectedCodec == index,
                                    onClick = { selectedCodec = index }
                                )
                            }
                        }
                    }

                    SettingSection(title = "画质") {
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            qualityOptions.forEachIndexed { index, (label, sub, _) ->
                                QualityChip(
                                    label = label,
                                    subLabel = sub,
                                    selected = selectedQuality == index,
                                    onClick = { selectedQuality = index }
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
                            val (width, height) = resOptions[selectedRes].second
                            val fps = listOf(24, 30, 60)[selectedFps]
                            val bitrate = qualityOptions[selectedQuality].third
                            val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
                            val path = "/sdcard/VideoSDK_$timestamp.mp4"
                            timelineVm.startExport(path, width, height, fps, bitrate)
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
                                    Brush.horizontalGradient(listOf(Accent, Color(0xFF7C5CFF))),
                                    RoundedCornerShape(26.dp)
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Icon(Icons.Filled.FileDownload, contentDescription = null, tint = Color.Black, modifier = Modifier.size(20.dp))
                                Text("开始导出", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 16.sp)
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
            .clip(RoundedCornerShape(14.dp))
            .background(Color(0xFF1A1A1A)),
        contentAlignment = Alignment.Center
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(Icons.Filled.Movie, contentDescription = null, tint = Color(0xFF555555), modifier = Modifier.size(48.dp))
            Spacer(Modifier.height(8.dp))
            Text("预览当前时间线并选择导出规格", color = Color(0xFF777777), fontSize = 13.sp)
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
        options.forEachIndexed { index, label ->
            OptionChip(label = label, selected = selectedIndex == index, onClick = { onSelect(index) })
        }
    }
}

@Composable
private fun OptionChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (selected) Accent else Color(0xFF1E1E1E))
            .border(1.dp, if (selected) Accent else Color(0xFF333333), RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = if (selected) Color.Black else Color.White, fontSize = 13.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun QualityChip(label: String, subLabel: String, selected: Boolean, onClick: () -> Unit) {
    Column(
        modifier = Modifier
            .clip(RoundedCornerShape(10.dp))
            .background(if (selected) Accent.copy(alpha = 0.16f) else Color(0xFF1E1E1E))
            .border(1.5.dp, if (selected) Accent else Color(0xFF333333), RoundedCornerShape(10.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(label, color = if (selected) Accent else Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
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
        SummaryRow("输出", "/sdcard/VideoSDK_*.mp4")
    }
}

@Composable
private fun SummaryRow(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
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
                color = Accent,
                strokeWidth = 8.dp,
                trackColor = Color(0xFF333333)
            )
            Text("${(progress * 100).toInt()}%", color = Color.White, fontSize = 24.sp, fontWeight = FontWeight.Bold)
        }
        Spacer(Modifier.height(32.dp))
        Text("正在导出...", color = Color.White, fontSize = 16.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text("后台导出队列后续可接入 WorkManager 和断点恢复。", color = TextSec, fontSize = 13.sp)
        Spacer(Modifier.height(32.dp))
        OutlinedButton(onClick = onCancel, shape = RoundedCornerShape(25.dp)) {
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
        Icon(Icons.Filled.CheckCircle, contentDescription = null, tint = Color(0xFF20C997), modifier = Modifier.size(72.dp))
        Spacer(Modifier.height(24.dp))
        Text("导出完成", color = Color.White, fontSize = 22.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text("成片已写入模拟输出路径。", color = TextSec, fontSize = 14.sp)
        Spacer(Modifier.height(40.dp))
        Button(
            onClick = onEditAgain,
            modifier = Modifier.fillMaxWidth().height(50.dp),
            colors = ButtonDefaults.buttonColors(containerColor = Accent),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text("继续编辑", color = Color.Black, fontWeight = FontWeight.Bold)
        }
        Spacer(Modifier.height(12.dp))
        OutlinedButton(onClick = onBackHome, modifier = Modifier.fillMaxWidth().height(50.dp), shape = RoundedCornerShape(25.dp)) {
            Text("回到创作台")
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
        Button(onClick = onRetry, colors = ButtonDefaults.buttonColors(containerColor = Accent), shape = RoundedCornerShape(25.dp)) {
            Text("重试", color = Color.Black, fontWeight = FontWeight.Bold)
        }
    }
}
