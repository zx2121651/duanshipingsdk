package com.sdk.video.sample.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
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

private val Orange = Color(0xFFFF6B00)
private val OrangeLight = Color(0xFFFF9A3C)
private val Surface = Color(0xFF1A1A1A)
private val SurfaceLight = Color(0xFF252525)
private val TextSecondary = Color(0xFF999999)

private data class FeatureCard(val icon: ImageVector, val label: String, val sub: String, val tint: Color)

private val FEATURE_CARDS = listOf(
    FeatureCard(Icons.Filled.AutoFixHigh, "一键成片", "AI 自动剪辑", Color(0xFF7C4DFF)),
    FeatureCard(Icons.Filled.GridView,    "模板",    "套用剪映风格", Color(0xFF00BCD4)),
    FeatureCard(Icons.Filled.MusicNote,   "卡点",    "节拍自动踩点", Color(0xFFE91E63)),
    FeatureCard(Icons.Filled.Subtitles,   "字幕",    "语音转文字",   Color(0xFF4CAF50)),
    FeatureCard(Icons.Filled.ColorLens,   "滤镜",    "专业调色包",   Color(0xFFFF9800)),
)

private data class RecentProject(val title: String, val duration: String, val date: String)

private val MOCK_RECENT = listOf(
    RecentProject("旅行 Vlog", "2:34", "今天"),
    RecentProject("生日记录", "1:15", "昨天"),
    RecentProject("产品宣传片", "0:58", "5月6日"),
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
            .background(Color(0xFF0D0D0D))
            .verticalScroll(rememberScrollState())
    ) {
        HomeHeader()
        CreateButtons(
            onNewProject = { navController.navigate("import") },
            onCapture    = { navController.navigate("capture") }
        )
        FeatureRow()
        if (clips.isNotEmpty()) {
            ContinueEditingBanner { navController.navigate("timeline_editor") }
        }
        RecentProjectsSection { navController.navigate("import") }
        Spacer(Modifier.height(24.dp))
    }
}

@Composable
private fun HomeHeader() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "VideoSDK Demo",
                color = Color.White,
                fontSize = 22.sp,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = "对标剪映 · SDK 全能力展示",
                color = TextSecondary,
                fontSize = 12.sp
            )
        }
        Box(
            modifier = Modifier
                .size(36.dp)
                .clip(CircleShape)
                .background(Surface),
            contentAlignment = Alignment.Center
        ) {
            Icon(Icons.Filled.Search, contentDescription = "搜索", tint = Color.White, modifier = Modifier.size(20.dp))
        }
    }
}

@Composable
private fun CreateButtons(onNewProject: () -> Unit, onCapture: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Button(
            onClick = onNewProject,
            modifier = Modifier
                .weight(1f)
                .height(52.dp),
            colors = ButtonDefaults.buttonColors(containerColor = Color.Transparent),
            contentPadding = PaddingValues(0.dp),
            shape = RoundedCornerShape(12.dp)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(
                        Brush.horizontalGradient(listOf(Orange, OrangeLight)),
                        RoundedCornerShape(12.dp)
                    ),
                contentAlignment = Alignment.Center
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Icon(Icons.Filled.Add, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
                    Text("新建创作", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                }
            }
        }

        OutlinedButton(
            onClick = onCapture,
            modifier = Modifier
                .weight(1f)
                .height(52.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.White),
            border = androidx.compose.foundation.BorderStroke(1.dp, Color(0xFF444444)),
            shape = RoundedCornerShape(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Icon(Icons.Filled.Videocam, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
                Text("实时拍摄", color = Color.White, fontSize = 15.sp)
            }
        }
    }
}

@Composable
private fun FeatureRow() {
    Column(modifier = Modifier.padding(top = 20.dp)) {
        Text(
            text = "功能",
            color = Color.White,
            fontSize = 16.sp,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.padding(horizontal = 20.dp, vertical = 8.dp)
        )
        LazyRow(
            contentPadding = PaddingValues(horizontal = 20.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(FEATURE_CARDS) { card ->
                FeatureCardItem(card)
            }
        }
    }
}

@Composable
private fun FeatureCardItem(card: FeatureCard) {
    Column(
        modifier = Modifier
            .width(76.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(SurfaceLight)
            .padding(vertical = 12.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Box(
            modifier = Modifier
                .size(40.dp)
                .clip(CircleShape)
                .background(card.tint.copy(alpha = 0.15f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(card.icon, contentDescription = card.label, tint = card.tint, modifier = Modifier.size(22.dp))
        }
        Text(card.label, color = Color.White, fontSize = 12.sp, fontWeight = FontWeight.Medium)
        Text(card.sub, color = TextSecondary, fontSize = 10.sp)
    }
}

@Composable
private fun ContinueEditingBanner(onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 12.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(Brush.horizontalGradient(listOf(Color(0xFF1E2A4A), Color(0xFF1A1E3A))))
            .clickable(onClick = onClick)
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(Icons.Filled.PlayArrow, contentDescription = null, tint = OrangeLight, modifier = Modifier.size(28.dp))
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text("继续编辑", color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
            Text("已导入片段，点击继续", color = TextSecondary, fontSize = 12.sp)
        }
        Icon(Icons.Filled.ChevronRight, contentDescription = null, tint = TextSecondary)
    }
}

@Composable
private fun RecentProjectsSection(onImportNew: () -> Unit) {
    Column(modifier = Modifier.padding(top = 20.dp)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "最近项目",
                color = Color.White,
                fontSize = 16.sp,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.weight(1f)
            )
            TextButton(onClick = {}) {
                Text("查看全部", color = TextSecondary, fontSize = 12.sp)
            }
        }

        MOCK_RECENT.forEach { proj ->
            RecentProjectRow(proj)
        }

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 8.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(SurfaceLight)
                .clickable(onClick = onImportNew)
                .padding(16.dp),
            contentAlignment = Alignment.Center
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(Icons.Filled.Add, contentDescription = null, tint = TextSecondary, modifier = Modifier.size(20.dp))
                Text("从相册导入新素材", color = TextSecondary, fontSize = 14.sp)
            }
        }
    }
}

@Composable
private fun RecentProjectRow(proj: RecentProject) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 4.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(SurfaceLight)
            .padding(12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(56.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(Color(0xFF333333)),
            contentAlignment = Alignment.Center
        ) {
            Icon(Icons.Filled.Movie, contentDescription = null, tint = Color(0xFF555555), modifier = Modifier.size(28.dp))
        }
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(proj.title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Medium)
            Text("时长 ${proj.duration} · ${proj.date}", color = TextSecondary, fontSize = 12.sp)
        }
        Icon(Icons.Filled.MoreVert, contentDescription = null, tint = Color(0xFF666666), modifier = Modifier.size(20.dp))
    }
}
