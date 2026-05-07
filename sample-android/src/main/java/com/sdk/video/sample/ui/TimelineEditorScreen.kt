package com.sdk.video.sample.ui

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
import androidx.navigation.NavController
import androidx.compose.runtime.collectAsState
import com.sdk.video.InternalApi
import com.sdk.video.sample.state.AppViewModel
import com.sdk.video.sample.state.TimelineViewModel
import com.sdk.video.sample.ui.components.ClipTrackView
import com.sdk.video.sample.ui.components.ToolBar
import com.sdk.video.sample.ui.components.ToolPanelContainer

private val Orange = Color(0xFFFF6B00)
private val PanelBg = Color(0xFF141414)

@OptIn(ExperimentalMaterial3Api::class, InternalApi::class)
@Composable
fun TimelineEditorScreen(
    navController: NavController,
    timelineVm: TimelineViewModel,
    appViewModel: AppViewModel
) {
    val clips         by timelineVm.clips.collectAsState()
    val selectedIndex by timelineVm.selectedIndex.collectAsState()
    val activeTool    by timelineVm.activeTool.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "剪辑",
                        color = Color.White,
                        fontWeight = FontWeight.Bold,
                        fontSize = 17.sp
                    )
                },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "返回", tint = Color.White)
                    }
                },
                actions = {
                    Button(
                        onClick = { navController.navigate("export") },
                        colors = ButtonDefaults.buttonColors(containerColor = Orange),
                        shape = RoundedCornerShape(20.dp),
                        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 0.dp),
                        modifier = Modifier.height(34.dp)
                    ) {
                        Text("导出", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 14.sp)
                    }
                    Spacer(Modifier.width(8.dp))
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF111111))
            )
        },
        containerColor = Color(0xFF0D0D0D)
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            val timelineHandle = timelineVm.timelineManager
                .collectAsState().value?.getNativeHandle() ?: 0L

            if (clips.isNotEmpty() && timelineHandle != 0L) {
                val clipPairs = clips.map { it.clipId to it.uri }
                TimelinePreviewSurface(
                    timelineHandle = timelineHandle,
                    clips          = clipPairs,
                    player         = timelineVm.player,
                    modifier       = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                )
            } else {
                EmptyPreviewArea(
                    hasClips = clips.isNotEmpty(),
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                )
            }

            ClipActionsBar(
                hasSelection = clips.isNotEmpty() && selectedIndex in clips.indices,
                onSplit = {},
                onDelete = { timelineVm.deleteClip() },
                onDuplicate = {}
            )

            ClipTrackView(
                clips = clips,
                selectedIndex = selectedIndex,
                onSelectClip = { timelineVm.selectClip(it) },
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF111111))
                    .padding(vertical = 8.dp)
            )

            ToolPanelContainer(
                activeTool = activeTool,
                timelineVm = timelineVm,
                onNavigateToText = { navController.navigate("text_editor") }
            )

            ToolBar(
                activeTool = activeTool,
                onToolClick = { timelineVm.setActiveTool(it) }
            )
        }
    }
}

@Composable
private fun EmptyPreviewArea(hasClips: Boolean, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier.background(Color.Black),
        contentAlignment = Alignment.Center
    ) {
        if (hasClips) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(16f / 9f)
                        .padding(horizontal = 16.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF1A1A1A)),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(
                            Icons.Filled.PlayCircle,
                            contentDescription = null,
                            tint = Color(0xFF555555),
                            modifier = Modifier.size(56.dp)
                        )
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "预览区域（需接入 MediaCodec 渲染）",
                            color = Color(0xFF555555),
                            fontSize = 12.sp
                        )
                    }
                }
                PlaybackControls()
            }
        } else {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(Icons.Filled.VideoLibrary, contentDescription = null, tint = Color(0xFF333333), modifier = Modifier.size(56.dp))
                Text("暂无素材", color = Color(0xFF555555), fontSize = 14.sp)
                Text("返回首页导入视频", color = Color(0xFF444444), fontSize = 12.sp)
            }
        }
    }
}

@Composable
private fun PlaybackControls() {
    var isPlaying by remember { mutableStateOf(false) }
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(24.dp)
    ) {
        Icon(
            Icons.Filled.SkipPrevious,
            contentDescription = "回到开始",
            tint = Color(0xFF888888),
            modifier = Modifier
                .size(28.dp)
                .clickable {}
        )
        Box(
            modifier = Modifier
                .size(44.dp)
                .clip(CircleShape)
                .background(Color(0xFF222222))
                .clickable { isPlaying = !isPlaying },
            contentAlignment = Alignment.Center
        ) {
            Icon(
                if (isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                contentDescription = if (isPlaying) "暂停" else "播放",
                tint = Color.White,
                modifier = Modifier.size(26.dp)
            )
        }
        Icon(
            Icons.Filled.SkipNext,
            contentDescription = "跳到末尾",
            tint = Color(0xFF888888),
            modifier = Modifier
                .size(28.dp)
                .clickable {}
        )
    }
}

@Composable
private fun ClipActionsBar(
    hasSelection: Boolean,
    onSplit: () -> Unit,
    onDelete: () -> Unit,
    onDuplicate: () -> Unit
) {
    if (!hasSelection) return

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF111111))
            .padding(horizontal = 16.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        ClipActionButton(icon = Icons.Filled.ContentCut, label = "分割", onClick = onSplit)
        ClipActionButton(icon = Icons.Filled.Delete, label = "删除", onClick = onDelete)
        ClipActionButton(icon = Icons.Filled.CopyAll, label = "复制", onClick = onDuplicate)
        Spacer(Modifier.weight(1f))
        Text(
            "向左滑动查看更多操作",
            color = Color(0xFF555555),
            fontSize = 11.sp,
            modifier = Modifier.align(Alignment.CenterVertically)
        )
    }
}

@Composable
private fun ClipActionButton(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    onClick: () -> Unit
) {
    Column(
        modifier = Modifier.clickable(onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(2.dp)
    ) {
        Icon(icon, contentDescription = label, tint = Color(0xFFCCCCCC), modifier = Modifier.size(20.dp))
        Text(label, color = Color(0xFF999999), fontSize = 10.sp)
    }
}
