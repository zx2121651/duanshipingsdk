package com.sdk.video.sample.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.CopyAll
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.PlayCircle
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.filled.VideoLibrary
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import com.sdk.video.InternalApi
import com.sdk.video.sample.state.AppViewModel
import com.sdk.video.sample.state.TimelineViewModel
import com.sdk.video.sample.ui.components.ClipTrackView
import com.sdk.video.sample.ui.components.ToolBar
import com.sdk.video.sample.ui.components.ToolPanelContainer

private val Accent = Color(0xFF00D7FF)

@OptIn(ExperimentalMaterial3Api::class, InternalApi::class)
@Composable
fun TimelineEditorScreen(
    navController: NavController,
    timelineVm: TimelineViewModel,
    appViewModel: AppViewModel
) {
    val clips by timelineVm.clips.collectAsState()
    val selectedIndex by timelineVm.selectedIndex.collectAsState()
    val activeTool by timelineVm.activeTool.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("剪辑工程", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 17.sp)
                        Text("${clips.size} 个片段 · 对标剪映时间线", color = Color(0xFF999999), fontSize = 11.sp)
                    }
                },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "返回", tint = Color.White)
                    }
                },
                actions = {
                    Button(
                        onClick = { navController.navigate("export") },
                        colors = ButtonDefaults.buttonColors(containerColor = Accent),
                        shape = RoundedCornerShape(20.dp),
                        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 0.dp),
                        modifier = Modifier.height(34.dp)
                    ) {
                        Icon(Icons.Filled.FileUpload, contentDescription = null, tint = Color.Black, modifier = Modifier.size(17.dp))
                        Spacer(Modifier.width(5.dp))
                        Text("导出", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 14.sp)
                    }
                    Spacer(Modifier.width(8.dp))
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF101010))
            )
        },
        containerColor = Color(0xFF0B0B0C)
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            val timelineHandle = timelineVm.timelineManager.collectAsState().value?.getNativeHandle() ?: 0L
            val positionUs by timelineVm.player.positionUs.collectAsState()
            val keyframes by timelineVm.keyframes.collectAsState()
            val hasKeyframeAtCurrent = remember(positionUs, keyframes) {
                keyframes.any { Math.abs(it - positionUs) < 100_000L }
            }

            if (clips.isNotEmpty() && timelineHandle != 0L) {
                val clipPairs = clips.map { it.clipId to it.uri }
                TimelinePreviewSurface(
                    timelineHandle = timelineHandle,
                    clips = clipPairs,
                    player = timelineVm.player,
                    hasKeyframeAtCurrent = hasKeyframeAtCurrent,
                    onToggleKeyframe = { timelineVm.toggleKeyframe(positionUs) },
                    modifier = Modifier
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
                timelineVm = timelineVm,
                selectedIndex = selectedIndex,
                onSelectClip = { timelineVm.selectClip(it) },
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF141416))
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
                        .clip(RoundedCornerShape(10.dp))
                        .background(Color(0xFF1A1A1A)),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(Icons.Filled.PlayCircle, contentDescription = null, tint = Color(0xFF555555), modifier = Modifier.size(56.dp))
                        Spacer(Modifier.height(8.dp))
                        Text("预览桥已就绪，等待 MediaCodec 渲染首帧", color = Color(0xFF777777), fontSize = 12.sp)
                    }
                }
                PlaybackControls()
            }
        } else {
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Icon(Icons.Filled.VideoLibrary, contentDescription = null, tint = Color(0xFF333333), modifier = Modifier.size(56.dp))
                Text("还没有素材", color = Color(0xFF777777), fontSize = 14.sp)
                Text("从首页导入视频，进入剪映式时间线编辑。", color = Color(0xFF555555), fontSize = 12.sp)
            }
        }
    }
}

@Composable
private fun PlaybackControls() {
    var isPlaying by remember { mutableStateOf(false) }
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(24.dp)) {
        Icon(Icons.Filled.SkipPrevious, contentDescription = "上一帧", tint = Color(0xFF888888), modifier = Modifier.size(28.dp).clickable {})
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
        Icon(Icons.Filled.SkipNext, contentDescription = "下一帧", tint = Color(0xFF888888), modifier = Modifier.size(28.dp).clickable {})
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
            .padding(horizontal = 16.dp, vertical = 7.dp),
        horizontalArrangement = Arrangement.spacedBy(18.dp)
    ) {
        ClipActionButton(icon = Icons.Filled.ContentCut, label = "分割", onClick = onSplit)
        ClipActionButton(icon = Icons.Filled.Delete, label = "删除", onClick = onDelete)
        ClipActionButton(icon = Icons.Filled.CopyAll, label = "复制", onClick = onDuplicate)
        Spacer(Modifier.weight(1f))
        Text(
            "选中片段后可调色、变速、加字幕和转场",
            color = Color(0xFF666666),
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
