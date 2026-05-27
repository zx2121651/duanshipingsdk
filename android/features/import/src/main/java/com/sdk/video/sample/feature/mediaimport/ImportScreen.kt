package com.sdk.video.sample.feature.mediaimport

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed as lazyRowItemsIndexed
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.navigation.NavController
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.size.Size
import com.sdk.video.sample.core.model.TimelineViewModel

private val Accent = Color(0xFF00D7FF)

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ImportScreen(
    navController: NavController,
    timelineVm: TimelineViewModel
) {
    val context = LocalContext.current
    var pickedUris by remember { mutableStateOf<List<Uri>>(emptyList()) }
    val selectedList = remember { mutableStateListOf<Uri>() }

    var showPreviewDialog by remember { mutableStateOf(false) }
    var previewStartIndex by remember { mutableStateOf(0) }

    // 调用系统选择器，支持视频和图片 (通配符 "*/*")
    val launcher = rememberLauncherForActivityResult(
        ActivityResultContracts.GetMultipleContents()
    ) { uris ->
        pickedUris = uris
        selectedList.clear()
        selectedList.addAll(uris)
    }

    LaunchedEffect(Unit) {
        if (pickedUris.isEmpty()) launcher.launch("*/*")
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
                    TextButton(onClick = { launcher.launch("*/*") }) {
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
                EmptyImportState { launcher.launch("*/*") }
            } else {
                Text(
                    text = "已选择 ${selectedList.size} 个素材，支持点击网格追加，可在底部滑动大图预览。",
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
                        val count = selectedList.count { it == uri }
                        VideoThumbnailCell(
                            uri = uri,
                            count = count,
                            onClick = {
                                selectedList.add(uri)
                            }
                        )
                    }
                }

                // 底部已选素材横向滚动列表 (支持删除，点击预览)
                if (selectedList.isNotEmpty()) {
                    LazyRow(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(Color(0xFF151515))
                            .padding(vertical = 8.dp, horizontal = 16.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        lazyRowItemsIndexed(selectedList) { index, uri ->
                            Box(
                                modifier = Modifier
                                    .size(54.dp, 96.dp)
                                    .clip(RoundedCornerShape(4.dp))
                                    .background(Color(0xFF222222))
                                    .clickable {
                                        previewStartIndex = index
                                        showPreviewDialog = true
                                    }
                            ) {
                                AsyncImage(
                                    model = ImageRequest.Builder(context)
                                        .data(uri)
                                        .decoderFactory(VideoFrameDecoder.Factory())
                                        .size(108, 192)
                                        .crossfade(true)
                                        .build(),
                                    contentDescription = null,
                                    contentScale = ContentScale.Crop,
                                    modifier = Modifier.fillMaxSize()
                                )
                                // 右上角删除叉叉
                                Box(
                                    modifier = Modifier
                                        .align(Alignment.TopEnd)
                                        .padding(2.dp)
                                        .size(16.dp)
                                        .clip(CircleShape)
                                        .background(Color(0xAA000000))
                                        .clickable { selectedList.removeAt(index) },
                                    contentAlignment = Alignment.Center
                                ) {
                                    Icon(
                                        Icons.Filled.Close,
                                        contentDescription = "删除",
                                        tint = Color.White,
                                        modifier = Modifier.size(10.dp)
                                    )
                                }
                            }
                        }
                    }
                }

                ConfirmBar(
                    count = selectedList.size,
                    enabled = selectedList.isNotEmpty()
                ) {
                    timelineVm.importClips(selectedList.toList(), context)
                    navController.navigate("timeline_editor") {
                        popUpTo("home") { saveState = false }
                    }
                }
            }
        }
    }

    // 全屏大图左右滑动预览弹窗
    if (showPreviewDialog && selectedList.isNotEmpty()) {
        Dialog(
            onDismissRequest = { showPreviewDialog = false },
            properties = DialogProperties(usePlatformDefaultWidth = false)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black)
            ) {
                val pagerState = rememberPagerState(
                    initialPage = previewStartIndex,
                    pageCount = { selectedList.size }
                )

                HorizontalPager(
                    state = pagerState,
                    modifier = Modifier.fillMaxSize()
                ) { page ->
                    if (page in selectedList.indices) {
                        val uri = selectedList[page]
                        Box(
                            modifier = Modifier.fillMaxSize(),
                            contentAlignment = Alignment.Center
                        ) {
                            AsyncImage(
                                model = ImageRequest.Builder(context)
                                    .data(uri)
                                    .decoderFactory(VideoFrameDecoder.Factory())
                                    .crossfade(true)
                                    .build(),
                                contentDescription = "大图预览",
                                contentScale = ContentScale.Fit,
                                modifier = Modifier.fillMaxWidth().aspectRatio(9f / 16f)
                            )
                        }
                    }
                }

                // 顶部控制与计数
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .align(Alignment.TopCenter)
                        .padding(top = 40.dp, start = 16.dp, end = 16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    IconButton(onClick = { showPreviewDialog = false }) {
                        Icon(Icons.Filled.Close, contentDescription = "关闭", tint = Color.White)
                    }
                    Text(
                        text = "${pagerState.currentPage + 1} / ${selectedList.size}",
                        color = Color.White,
                        fontSize = 16.sp,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(Modifier.width(48.dp))
                }
            }
        }
    }
}

@Composable
private fun VideoThumbnailCell(
    uri: Uri,
    count: Int,
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
                if (count > 0) Modifier.border(2.dp, Accent, RoundedCornerShape(4.dp))
                else Modifier
            )
    ) {
        AsyncImage(
            model = ImageRequest.Builder(context)
                .data(uri)
                .decoderFactory(VideoFrameDecoder.Factory())
                .size(300, 530)
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

        if (count > 0) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(6.dp)
                    .size(22.dp)
                    .clip(CircleShape)
                    .background(Accent),
                contentAlignment = Alignment.Center
            ) {
                Text(count.toString(), color = Color.Black, fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
        } else {
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
        Text("选择媒体素材", color = Color(0xFFCCCCCC), fontSize = 16.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text("支持视频与照片，可重复添加至主视频轨。", color = Color(0xFF666666), fontSize = 13.sp)
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
