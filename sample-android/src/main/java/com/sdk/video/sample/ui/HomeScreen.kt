package com.sdk.video.sample.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Subtitles
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import com.sdk.video.sample.state.AppViewModel
import com.sdk.video.sample.state.TimelineViewModel

private val Accent = Color(0xFF00D7FF)       // 剪映经典青色/青橙
private val Accent2 = Color(0xFF7C5CFF)      // 极客紫
private val Surface = Color(0xFF141416)       // 深色卡片背景
private val SurfaceLight = Color(0xFF222225)  // 浅色卡片背景
private val TextSecondary = Color(0xFF888892) // 辅助文字颜色
private val BorderColor = Color(0xFF2B2B30)   // 卡片边框颜色

private data class QuickEntry(
    val icon: ImageVector,
    val title: String,
    val subtitle: String,
    val tint: Color,
    val route: String
)

private data class DraftCard(
    val title: String,
    val duration: String,
    val segmentCount: Int,
    val info: String,
    val timeStr: String,
    val accent: Color
)

private val quickEntries = listOf(
    QuickEntry(Icons.Filled.AutoAwesome, "AI 一键成片", "智能卡点与剪辑", Color(0xFF7C5CFF), "import"),
    QuickEntry(Icons.Filled.Subtitles, "智能字幕", "语音识别一键匹配", Color(0xFF00C9A7), "text_editor"),
    QuickEntry(Icons.Filled.AutoFixHigh, "热门特效", "实时分割与滤镜", Color(0xFFFF5C8A), "effects"),
    QuickEntry(Icons.Filled.GraphicEq, "音乐卡点", "节拍检测自动对齐", Color(0xFFFFB020), "capture"),
    QuickEntry(Icons.Filled.Tune, "专业调色", "电影级色彩调节", Color(0xFF00BCD4), "timeline_editor"),
    QuickEntry(Icons.Filled.Speed, "曲线变速", "慢动作与动作平滑", Color(0xFFFF7043), "timeline_editor"),
)

private val draftCards = listOf(
    DraftCard("2026 毕业季 · 纪念纪录片", "03:15", 12, "1080P · 30fps", "刚刚", Color(0xFF00D7FF)),
    DraftCard("Weekend Vlog · 露营日记", "01:45", 8, "4K · 60fps", "昨天", Color(0xFFFFB020)),
    DraftCard("国风卡点大片 · 剪辑工程", "00:30", 5, "1080P · 60fps", "3天前", Color(0xFF7C5CFF)),
)

@Composable
fun HomeScreen(
    navController: NavController,
    appViewModel: AppViewModel,
    timelineVm: TimelineViewModel
) {
    val clips by timelineVm.clips.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(Color(0xFF18181C), Color(0xFF0B0B0D))
                )
            )
            .verticalScroll(rememberScrollState())
    ) {
        Header()
        HeroProjectActions(
            onNewProject = { navController.navigate("import") },
            onCapture = { navController.navigate("capture") }
        )
        QuickEntryRow { route -> navController.navigate(route) }

        if (clips.isNotEmpty()) {
            ContinueEditingBanner(
                count = clips.size,
                onClick = { navController.navigate("timeline_editor") }
            )
        }

        DraftSection(
            onOpenDraft = { navController.navigate("timeline_editor") },
            onImportNew = { navController.navigate("import") }
        )
        CapabilityStrip()
        Spacer(Modifier.height(36.dp))
    }
}

@Composable
private fun Header() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 20.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "创作中心",
                color = Color.White,
                fontSize = 24.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 0.5.sp
            )
            Text(
                text = "专业剪辑 · 智能特效 · 极致画质",
                color = TextSecondary,
                fontSize = 12.sp,
                fontWeight = FontWeight.Medium
            )
        }
        Box(
            modifier = Modifier
                .size(42.dp)
                .clip(CircleShape)
                .background(Color(0xFF1E1E22))
                .border(1.dp, Color(0xFF323238), CircleShape)
                .clickable { /* settings or profile */ },
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Filled.ContentCut,
                contentDescription = "剪辑",
                tint = Accent,
                modifier = Modifier.size(20.dp)
            )
        }
    }
}

@Composable
private fun HeroProjectActions(onNewProject: () -> Unit, onCapture: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp)
            .shadow(16.dp, RoundedCornerShape(20.dp))
            .clip(RoundedCornerShape(20.dp))
            .background(
                Brush.linearGradient(
                    colors = listOf(Color(0xFF1D263B), Color(0xFF131316))
                )
            )
            .border(1.dp, Color(0xFF2E3D5E), RoundedCornerShape(20.dp))
            .padding(20.dp)
    ) {
        Text(
            text = "开启你的高光时刻",
            color = Color.White,
            fontSize = 21.sp,
            fontWeight = FontWeight.ExtraBold
        )
        Spacer(Modifier.height(4.dp))
        Text(
            text = "支持多轨时间线剪辑、4K无损导出和实时AI人像分割",
            color = TextSecondary,
            fontSize = 12.sp,
            fontWeight = FontWeight.Normal
        )
        Spacer(Modifier.height(20.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            Button(
                onClick = onNewProject,
                modifier = Modifier
                    .weight(1.3f)
                    .height(54.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Accent),
                shape = RoundedCornerShape(16.dp),
                elevation = ButtonDefaults.buttonElevation(defaultElevation = 4.dp)
            ) {
                Icon(
                    imageVector = Icons.Filled.Add,
                    contentDescription = null,
                    tint = Color.Black,
                    modifier = Modifier.size(22.dp)
                )
                Spacer(Modifier.width(6.dp))
                Text(
                    text = "开始创作",
                    color = Color.Black,
                    fontWeight = FontWeight.Black,
                    fontSize = 15.sp
                )
            }
            OutlinedButton(
                onClick = onCapture,
                modifier = Modifier
                    .weight(1f)
                    .height(54.dp),
                border = BorderStroke(1.2.dp, Color(0xFF4A4A58)),
                shape = RoundedCornerShape(16.dp),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.White)
            ) {
                Icon(
                    imageVector = Icons.Filled.Videocam,
                    contentDescription = null,
                    tint = Color.White,
                    modifier = Modifier.size(20.dp)
                )
                Spacer(Modifier.width(6.dp))
                Text(
                    text = "拍同款",
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontSize = 14.sp
                )
            }
        }
    }
}

@Composable
private fun QuickEntryRow(onOpen: (String) -> Unit) {
    Column(modifier = Modifier.padding(top = 26.dp)) {
        SectionTitle("智能工具箱", "对标剪映核心 SDK 能力")
        LazyRow(
            contentPadding = PaddingValues(horizontal = 20.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            items(quickEntries) { item ->
                Column(
                    modifier = Modifier
                        .width(120.dp)
                        .height(130.dp)
                        .clip(RoundedCornerShape(16.dp))
                        .background(Surface)
                        .border(1.dp, BorderColor, RoundedCornerShape(16.dp))
                        .clickable { onOpen(item.route) }
                        .padding(14.dp),
                    verticalArrangement = Arrangement.SpaceBetween
                ) {
                    Box(
                        modifier = Modifier
                            .size(40.dp)
                            .clip(CircleShape)
                            .background(item.tint.copy(alpha = 0.12f))
                            .border(1.dp, item.tint.copy(alpha = 0.25f), CircleShape),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = item.icon,
                            contentDescription = item.title,
                            tint = item.tint,
                            modifier = Modifier.size(22.dp)
                        )
                    }
                    Column {
                        Text(
                            text = item.title,
                            color = Color.White,
                            fontSize = 13.sp,
                            fontWeight = FontWeight.Bold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Spacer(Modifier.height(2.dp))
                        Text(
                            text = item.subtitle,
                            color = TextSecondary,
                            fontSize = 10.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ContinueEditingBanner(count: Int, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 20.dp)
            .shadow(8.dp, RoundedCornerShape(16.dp))
            .clip(RoundedCornerShape(16.dp))
            .background(Color(0xFF142B30))
            .border(1.dp, Color(0xFF1C454C), RoundedCornerShape(16.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 18.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(36.dp)
                .clip(CircleShape)
                .background(Accent.copy(alpha = 0.2f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Filled.PlayArrow,
                contentDescription = null,
                tint = Accent,
                modifier = Modifier.size(24.dp)
            )
        }
        Spacer(Modifier.width(14.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "继续上次的视频剪辑",
                color = Color.White,
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = "时间线中已有 $count 个片段未导出",
                color = Accent.copy(alpha = 0.8f),
                fontSize = 11.sp,
                fontWeight = FontWeight.Medium
            )
        }
        Text(
            text = "恢复工程",
            color = Accent,
            fontSize = 13.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

@Composable
private fun DraftSection(onOpenDraft: () -> Unit, onImportNew: () -> Unit) {
    Column(modifier = Modifier.padding(top = 20.dp)) {
        SectionTitle("最近草稿", "本地自动保存，随时继续创作")
        draftCards.forEach { draft ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp, vertical = 6.dp)
                    .clip(RoundedCornerShape(16.dp))
                    .background(Surface)
                    .border(1.dp, BorderColor, RoundedCornerShape(16.dp))
                    .clickable(onClick = onOpenDraft)
                    .padding(14.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Draft Cover Box mimicking actual videos
                Box(
                    modifier = Modifier
                        .size(80.dp, 56.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(
                            Brush.linearGradient(
                                colors = listOf(draft.accent.copy(alpha = 0.2f), Color(0xFF1E1E24))
                            )
                        )
                        .border(1.dp, BorderColor, RoundedCornerShape(8.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Filled.Movie,
                        contentDescription = null,
                        tint = draft.accent,
                        modifier = Modifier.size(24.dp)
                    )
                    // Duration badge overlayed
                    Box(
                        modifier = Modifier
                            .align(Alignment.BottomEnd)
                            .padding(4.dp)
                            .background(Color(0xBB000000), RoundedCornerShape(3.dp))
                            .padding(horizontal = 4.dp, vertical = 1.dp)
                    ) {
                        Text(
                            text = draft.duration,
                            color = Color.White,
                            fontSize = 9.sp,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
                Spacer(Modifier.width(14.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = draft.title,
                        color = Color.White,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Spacer(Modifier.height(3.dp))
                    Text(
                        text = "${draft.segmentCount} 个分段 · ${draft.info}",
                        color = TextSecondary,
                        fontSize = 11.sp
                    )
                    Spacer(Modifier.height(2.dp))
                    Text(
                        text = "更新时间: ${draft.timeStr}",
                        color = TextSecondary.copy(alpha = 0.8f),
                        fontSize = 10.sp
                    )
                }
                IconButton(onClick = { /* menu actions */ }) {
                    Icon(
                        imageVector = Icons.Filled.MoreVert,
                        contentDescription = "菜单",
                        tint = TextSecondary,
                        modifier = Modifier.size(20.dp)
                    )
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp)
                .clip(RoundedCornerShape(12.dp))
                .clickable(onClick = onImportNew)
                .padding(vertical = 12.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Filled.PhotoLibrary,
                contentDescription = null,
                tint = Accent,
                modifier = Modifier.size(18.dp)
            )
            Spacer(Modifier.width(8.dp))
            Text(
                text = "导入本地素材，新建项目",
                color = Accent,
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold
            )
        }
    }
}

@Composable
private fun CapabilityStrip() {
    Column(modifier = Modifier.padding(horizontal = 20.dp, vertical = 18.dp)) {
        SectionTitle("SDK 核心引擎能力", "底层高性能 C++ / EGL 驱动")
        Row(
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            listOf("4K 零拷贝", "实时 AI 隔离", "多轨并发解码", "硬件级导出").forEachIndexed { index, text ->
                val color = listOf(Accent, Accent2, Color(0xFFFFB020), Color(0xFF00C9A7))[index]
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(10.dp))
                        .background(color.copy(alpha = 0.1f))
                        .border(1.dp, color.copy(alpha = 0.2f), RoundedCornerShape(10.dp))
                        .padding(vertical = 12.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = text,
                        color = color,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.ExtraBold,
                        textAlign = TextAlign.Center
                    )
                }
            }
        }
    }
}

@Composable
private fun SectionTitle(title: String, subtitle: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = title,
            color = Color.White,
            fontSize = 16.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(Modifier.width(10.dp))
        Text(
            text = subtitle,
            color = TextSecondary,
            fontSize = 11.sp,
            fontWeight = FontWeight.Medium
        )
    }
}
