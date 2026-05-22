package com.sdk.video.sample.feature.import

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.size.Size
import com.sdk.video.sample.core.model.TimelineViewModel

private val Accent = Color(0xFF00D7FF)

/**
 * 视频素材选择与导入界面 - Feature Import Component
 * 
 * 核心逻辑与功能:
 * 1. 相册文件多选：结合 Jetpack Compose 架构中的 ActivityResult 契约拉起系统相册选择器。
 * 2. 视频帧解码展示：利用 [Coil] 库的 [VideoFrameDecoder] 在后台动态提取选中视频文件的首帧图像，缓存渲染为平铺网格。
 * 3. 动态时间线加载：记录用户点选的素材排序号，调用 [TimelineViewModel.importClips] 生成含有多个视频片段轨道的时间线。
 * 4. 路由扭转：导入完毕后将时间线传递到 `timeline_editor` 编辑页面。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ImportScreen(
    navController: NavController,
    timelineVm: TimelineViewModel
) {
    val context = LocalContext.current
    var pickedUris by remember { mutableStateOf<List<Uri>>(emptyList()) }
    val selectedSet = remember { mutableStateListOf<Uri>() }

    // 调用系统 GetMultipleContents 多选媒介协定
    val launcher = rememberLauncherForActivityResult(
        ActivityResultContracts.GetMultipleContents()
    ) { uris ->
        pickedUris = uris
        selectedSet.clear()
        selectedSet.addAll(uris)
    }

    // 页面初次进入时，自动拉起相册选择器
    LaunchedEffect(Unit) {
        if (pickedUris.isEmpty()) launcher.launch("video/*")
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("导入素材", color = Color.White, fontWeight = FontWeight.Bold) },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "返回", tint = Color.White)
                    }
                },
                actions = {
                    TextButton(onClick = { launcher.launch("video/*") }) {
                        Text("重新选择", color = Accent, fontSize = 14.sp)
                    }
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
            if (pickedUris.isEmpty()) {
                EmptyImportState { launcher.launch("video/*") }
            } else {
                Text(
                    text = "已选择 ${selectedSet.size}/${pickedUris.size} 个素材，按选择顺序生成时间线。",
                    color = Color(0xFF999999),
                    fontSize = 13.sp,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)
                )
                LazyVerticalGrid(
                    columns = GridCells.Fixed(3),
                    modifier = Modifier
                        .weight(1f)
                        .padding(horizontal = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(2.dp),
                    verticalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    itemsIndexed(pickedUris) { _, uri ->
                        VideoThumbnailCell(
                            uri = uri,
                            selected = uri in selectedSet,
                            selectionIndex = if (uri in selectedSet) selectedSet.indexOf(uri) + 1 else null,
                            onClick = {
                                if (uri in selectedSet) selectedSet.remove(uri)
                                else selectedSet.add(uri)
                            }
                        )
                    }
                }

                ConfirmBar(
                    count = selectedSet.size,
                    enabled = selectedSet.isNotEmpty()
                ) {
                    // 调用核心层封装的数据源，将 Uri 解析后加载至 SDK 时间线轨
                    timelineVm.importClips(selectedSet.toList(), context)
                    navController.navigate("timeline_editor") {
                        popUpTo("home") { saveState = false }
                    }
                }
            }
        }
    }
}

@Composable
private fun VideoThumbnailCell(
    uri: Uri,
    selected: Boolean,
    selectionIndex: Int?,
    onClick: () -> Unit
) {
    val context = LocalContext.current
    Box(
        modifier = Modifier
            .aspectRatio(9f / 16f)
            .clip(RoundedCornerShape(4.dp))
            .background(Color(0xFF1A1A1A))
            .clickable(onClick = onClick)
            .then(
                if (selected) Modifier.border(2.dp, Accent, RoundedCornerShape(4.dp))
                else Modifier
            )
    ) {
        // 利用 Coil 框架对指定本地 Uri 的视频资产异步取帧渲染
        AsyncImage(
            model = ImageRequest.Builder(context)
                .data(uri)
                .decoderFactory(VideoFrameDecoder.Factory()) // 专门用于解码本地 MP4 / WebM 的视频帧处理器
                .size(Size(300, 530))
                .crossfade(true)
                .build(),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxSize()
        )

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(0x22000000))
        )

        // 呈现被用户选中的序号徽章
        if (selected && selectionIndex != null) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(6.dp)
                    .size(22.dp)
                    .clip(CircleShape)
                    .background(Accent),
                contentAlignment = Alignment.Center
            ) {
                Text(selectionIndex.toString(), color = Color.Black, fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
        } else if (!selected) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(6.dp)
                    .size(22.dp)
                    .clip(CircleShape)
                    .background(Color(0x88000000))
                    .border(1.dp, Color.White, CircleShape)
            )
        }

        Icon(
            Icons.Filled.Movie,
            contentDescription = null,
            tint = Color(0x88FFFFFF),
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(4.dp)
                .size(16.dp)
        )
    }
}

@Composable
private fun ConfirmBar(count: Int, enabled: Boolean, onConfirm: () -> Unit) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF111111))
            .padding(horizontal = 20.dp, vertical = 12.dp)
    ) {
        Button(
            onClick = onConfirm,
            enabled = enabled,
            modifier = Modifier
                .fillMaxWidth()
                .height(50.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = Accent,
                disabledContainerColor = Color(0xFF444444)
            ),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text(
                text = if (count > 0) "添加到时间线 · $count 个素材" else "请选择素材",
                color = if (enabled) Color.Black else Color.White,
                fontWeight = FontWeight.Bold,
                fontSize = 16.sp
            )
        }
    }
}

@Composable
private fun EmptyImportState(onPick: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(Icons.Filled.PhotoLibrary, contentDescription = null, tint = Color(0xFF444444), modifier = Modifier.size(72.dp))
        Spacer(Modifier.height(16.dp))
        Text("选择视频素材", color = Color(0xFFCCCCCC), fontSize = 16.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text("支持多选，Demo 会按选择顺序创建主视频轨。", color = Color(0xFF666666), fontSize = 13.sp)
        Spacer(Modifier.height(24.dp))
        Button(
            onClick = onPick,
            colors = ButtonDefaults.buttonColors(containerColor = Accent),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text("打开相册", color = Color.Black, fontWeight = FontWeight.Bold)
        }
    }
}
