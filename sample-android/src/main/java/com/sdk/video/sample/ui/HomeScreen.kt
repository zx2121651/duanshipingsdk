package com.sdk.video.sample.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.width
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
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Subtitles
import androidx.compose.material.icons.filled.Title
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import com.sdk.video.sample.state.AppViewModel
import com.sdk.video.sample.state.TimelineViewModel

private val Accent = Color(0xFF00D7FF)
private val Accent2 = Color(0xFF7C5CFF)
private val Surface = Color(0xFF181818)
private val SurfaceLight = Color(0xFF242424)
private val TextSecondary = Color(0xFF9A9A9A)

private data class QuickEntry(
    val icon: ImageVector,
    val title: String,
    val subtitle: String,
    val tint: Color,
    val route: String
)

private data class DraftCard(
    val title: String,
    val meta: String,
    val accent: Color
)

private val quickEntries = listOf(
    QuickEntry(Icons.Filled.AutoAwesome, "AI 一键成片", "脚本、卡点、字幕", Color(0xFF7C5CFF), "import"),
    QuickEntry(Icons.Filled.Subtitles, "智能字幕", "语音识别与样式", Color(0xFF20C997), "text_editor"),
    QuickEntry(Icons.Filled.AutoFixHigh, "特效模板", "滤镜、妆容、道具", Color(0xFFFF5C8A), "effects"),
    QuickEntry(Icons.Filled.GraphicEq, "音乐卡点", "节拍同步与分段", Color(0xFFFFB020), "capture"),
    QuickEntry(Icons.Filled.Tune, "专业调色", "亮度、对比、锐化", Color(0xFF00BCD4), "timeline_editor"),
    QuickEntry(Icons.Filled.Speed, "变速曲线", "慢动作与节奏", Color(0xFFFF7043), "timeline_editor"),
)

private val draftCards = listOf(
    DraftCard("城市 Vlog 调色版", "9 个片段 · 01:28 · 今天", Color(0xFF00D7FF)),
    DraftCard("产品短片节奏版", "5 个片段 · 00:42 · 昨天", Color(0xFFFFB020)),
    DraftCard("口播字幕模板", "3 个片段 · 02:10 · 本周", Color(0xFF7C5CFF)),
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
            .background(Color(0xFF0B0B0C))
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
        Spacer(Modifier.height(28.dp))
    }
}

@Composable
private fun Header() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 18.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text("VideoSDK 创作台", color = Color.White, fontSize = 23.sp, fontWeight = FontWeight.Bold)
            Text("拍摄、剪辑、特效、导出的一体化 Android Demo", color = TextSecondary, fontSize = 12.sp)
        }
        Box(
            modifier = Modifier
                .size(38.dp)
                .clip(CircleShape)
                .background(SurfaceLight),
            contentAlignment = Alignment.Center
        ) {
            Icon(Icons.Filled.ContentCut, contentDescription = "创作", tint = Accent, modifier = Modifier.size(20.dp))
        }
    }
}

@Composable
private fun HeroProjectActions(onNewProject: () -> Unit, onCapture: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp)
            .clip(RoundedCornerShape(18.dp))
            .background(Brush.horizontalGradient(listOf(Color(0xFF141E30), Color(0xFF111111))))
            .padding(18.dp)
    ) {
        Text("从素材到成片", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Text("对标剪映主流程：导入素材、时间线编辑、字幕特效、高清导出。", color = TextSecondary, fontSize = 12.sp)
        Spacer(Modifier.height(16.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(
                onClick = onNewProject,
                modifier = Modifier.weight(1f).height(50.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Accent),
                shape = RoundedCornerShape(14.dp)
            ) {
                Icon(Icons.Filled.Add, contentDescription = null, tint = Color.Black, modifier = Modifier.size(20.dp))
                Spacer(Modifier.width(6.dp))
                Text("开始创作", color = Color.Black, fontWeight = FontWeight.Bold)
            }
            OutlinedButton(
                onClick = onCapture,
                modifier = Modifier.weight(1f).height(50.dp),
                border = BorderStroke(1.dp, Color(0xFF444444)),
                shape = RoundedCornerShape(14.dp)
            ) {
                Icon(Icons.Filled.Videocam, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
                Spacer(Modifier.width(6.dp))
                Text("拍同款", color = Color.White, fontWeight = FontWeight.SemiBold)
            }
        }
    }
}

@Composable
private fun QuickEntryRow(onOpen: (String) -> Unit) {
    Column(modifier = Modifier.padding(top = 22.dp)) {
        SectionTitle("创作工具", "SDK 能力入口")
        LazyRow(
            contentPadding = PaddingValues(horizontal = 20.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(quickEntries) { item ->
                Column(
                    modifier = Modifier
                        .width(116.dp)
                        .height(122.dp)
                        .clip(RoundedCornerShape(14.dp))
                        .background(Surface)
                        .clickable { onOpen(item.route) }
                        .padding(12.dp),
                    verticalArrangement = Arrangement.SpaceBetween
                ) {
                    Box(
                        modifier = Modifier
                            .size(38.dp)
                            .clip(CircleShape)
                            .background(item.tint.copy(alpha = 0.18f)),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(item.icon, contentDescription = item.title, tint = item.tint, modifier = Modifier.size(21.dp))
                    }
                    Column {
                        Text(item.title, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Bold)
                        Text(item.subtitle, color = TextSecondary, fontSize = 10.sp)
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
            .padding(horizontal = 20.dp, vertical = 18.dp)
            .clip(RoundedCornerShape(14.dp))
            .background(Color(0xFF14252A))
            .clickable(onClick = onClick)
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(Icons.Filled.PlayArrow, contentDescription = null, tint = Accent, modifier = Modifier.size(30.dp))
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text("继续编辑当前工程", color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
            Text("$count 个片段已在时间线中", color = TextSecondary, fontSize = 12.sp)
        }
        Text("打开", color = Accent, fontSize = 13.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun DraftSection(onOpenDraft: () -> Unit, onImportNew: () -> Unit) {
    Column(modifier = Modifier.padding(top = 18.dp)) {
        SectionTitle("最近草稿", "模拟剪辑项目")
        draftCards.forEach { draft ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp, vertical = 5.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(Surface)
                    .clickable(onClick = onOpenDraft)
                    .padding(12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(58.dp)
                        .clip(RoundedCornerShape(10.dp))
                        .background(draft.accent.copy(alpha = 0.22f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(Icons.Filled.Movie, contentDescription = null, tint = draft.accent, modifier = Modifier.size(28.dp))
                }
                Spacer(Modifier.width(12.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(draft.title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                    Text(draft.meta, color = TextSecondary, fontSize = 12.sp)
                }
            }
        }
        TextButton(
            onClick = onImportNew,
            modifier = Modifier.padding(horizontal = 14.dp)
        ) {
            Icon(Icons.Filled.PhotoLibrary, contentDescription = null, tint = Accent, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("导入素材新建工程", color = Accent)
        }
    }
}

@Composable
private fun CapabilityStrip() {
    Column(modifier = Modifier.padding(horizontal = 20.dp, vertical = 12.dp)) {
        SectionTitle("SDK 对标能力", "拍摄 + 特效 + 编辑 + 导出")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
            listOf("实时美颜", "道具贴纸", "多轨编辑", "后台导出").forEachIndexed { index, text ->
                val color = listOf(Accent, Accent2, Color(0xFFFFB020), Color(0xFF20C997))[index]
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(10.dp))
                        .background(color.copy(alpha = 0.16f))
                        .padding(vertical = 10.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(text, color = color, fontSize = 12.sp, fontWeight = FontWeight.Bold)
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
            .padding(horizontal = 20.dp, vertical = 8.dp),
        verticalAlignment = Alignment.Bottom
    ) {
        Text(title, color = Color.White, fontSize = 16.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.width(8.dp))
        Text(subtitle, color = TextSecondary, fontSize = 11.sp)
    }
}
