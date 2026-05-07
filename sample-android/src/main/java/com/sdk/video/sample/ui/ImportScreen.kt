package com.sdk.video.sample.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material.icons.filled.PhotoLibrary
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
import androidx.navigation.NavController
import coil.compose.AsyncImage
import coil.request.ImageRequest
import coil.size.Size
import coil.decode.VideoFrameDecoder
import com.sdk.video.sample.state.TimelineViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ImportScreen(
    navController: NavController,
    timelineVm: TimelineViewModel
) {
    val context = LocalContext.current
    var pickedUris by remember { mutableStateOf<List<Uri>>(emptyList()) }
    val selectedSet = remember { mutableStateListOf<Uri>() }

    val launcher = rememberLauncherForActivityResult(
        ActivityResultContracts.GetMultipleContents()
    ) { uris ->
        pickedUris = uris
        selectedSet.clear()
        selectedSet.addAll(uris)
    }

    LaunchedEffect(Unit) {
        if (pickedUris.isEmpty()) launcher.launch("video/*")
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("选择素材", color = Color.White, fontWeight = FontWeight.Bold) },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "返回", tint = Color.White)
                    }
                },
                actions = {
                    TextButton(onClick = { launcher.launch("video/*") }) {
                        Text("重新选择", color = Color(0xFFFF6B00), fontSize = 14.sp)
                    }
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
            if (pickedUris.isEmpty()) {
                EmptyImportState { launcher.launch("video/*") }
            } else {
                Text(
                    text = "已选 ${selectedSet.size}/${pickedUris.size} 个视频",
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
                    itemsIndexed(pickedUris) { index, uri ->
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
                if (selected) Modifier.border(2.dp, Color(0xFFFF6B00), RoundedCornerShape(4.dp))
                else Modifier
            )
    ) {
        AsyncImage(
            model = ImageRequest.Builder(context)
                .data(uri)
                .decoderFactory(VideoFrameDecoder.Factory())
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

        if (selected && selectionIndex != null) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(6.dp)
                    .size(22.dp)
                    .clip(CircleShape)
                    .background(Color(0xFFFF6B00)),
                contentAlignment = Alignment.Center
            ) {
                Text(selectionIndex.toString(), color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.Bold)
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
            .navigationBarsPadding()
            .padding(horizontal = 20.dp, vertical = 12.dp)
    ) {
        Button(
            onClick = onConfirm,
            enabled = enabled,
            modifier = Modifier
                .fillMaxWidth()
                .height(50.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = Color(0xFFFF6B00),
                disabledContainerColor = Color(0xFF444444)
            ),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text(
                text = if (count > 0) "添加到创作（$count 个）" else "请选择视频",
                color = Color.White,
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
        Icon(
            Icons.Filled.PhotoLibrary,
            contentDescription = null,
            tint = Color(0xFF444444),
            modifier = Modifier.size(72.dp)
        )
        Spacer(Modifier.height(16.dp))
        Text("未选择视频", color = Color(0xFF888888), fontSize = 16.sp)
        Spacer(Modifier.height(8.dp))
        Text("点击下方按钮从相册选择", color = Color(0xFF555555), fontSize = 13.sp)
        Spacer(Modifier.height(24.dp))
        Button(
            onClick = onPick,
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFFF6B00)),
            shape = RoundedCornerShape(25.dp)
        ) {
            Text("从相册选择", color = Color.White, fontWeight = FontWeight.Bold)
        }
    }
}
