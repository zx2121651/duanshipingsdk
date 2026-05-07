package com.sdk.video.sample.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
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
import coil.compose.AsyncImage
import coil.request.ImageRequest
import coil.size.Size
import coil.decode.VideoFrameDecoder
import com.sdk.video.sample.state.ClipItem

private val Orange = Color(0xFFFF6B00)

@Composable
fun ClipTrackView(
    clips: List<ClipItem>,
    selectedIndex: Int,
    onSelectClip: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val listState = rememberLazyListState()

    Column(modifier = modifier) {
        Text(
            text = "视频轨道",
            color = Color(0xFF888888),
            fontSize = 11.sp,
            modifier = Modifier.padding(start = 16.dp, bottom = 4.dp)
        )
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(80.dp)
        ) {
            if (clips.isEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(horizontal = 16.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF1E1E1E))
                        .border(1.dp, Color(0xFF333333), RoundedCornerShape(8.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Text("暂无片段", color = Color(0xFF555555), fontSize = 13.sp)
                }
            } else {
                LazyRow(
                    state = listState,
                    contentPadding = PaddingValues(horizontal = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(3.dp),
                    modifier = Modifier.fillMaxSize()
                ) {
                    itemsIndexed(clips) { index, clip ->
                        ClipCell(
                            clip = clip,
                            isSelected = index == selectedIndex,
                            onClick = { onSelectClip(index) }
                        )
                    }
                }
                TimelineCursor()
            }
        }

        if (clips.isNotEmpty()) {
            AudioWaveformStrip(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(24.dp)
                    .padding(horizontal = 16.dp)
            )
        }
    }
}

@Composable
private fun ClipCell(
    clip: ClipItem,
    isSelected: Boolean,
    onClick: () -> Unit
) {
    val context = LocalContext.current
    val durationSec = (clip.durationUs / 1_000_000f)
    val cellWidthDp = (durationSec * 36f).coerceIn(60f, 200f)

    Box(
        modifier = Modifier
            .width(cellWidthDp.dp)
            .fillMaxHeight()
            .clip(RoundedCornerShape(6.dp))
            .clickable(onClick = onClick)
            .then(
                if (isSelected) Modifier.border(2.dp, Orange, RoundedCornerShape(6.dp))
                else Modifier.border(1.dp, Color(0xFF333333), RoundedCornerShape(6.dp))
            )
    ) {
        AsyncImage(
            model = ImageRequest.Builder(context)
                .data(clip.uri)
                .decoderFactory(VideoFrameDecoder.Factory())
                .size(Size(200, 160))
                .crossfade(false)
                .build(),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxSize()
        )

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(if (isSelected) 0x22FF6B00 else 0x44000000))
        )

        if (isSelected) {
            TrimHandle(Modifier.align(Alignment.CenterStart).padding(start = 2.dp))
            TrimHandle(Modifier.align(Alignment.CenterEnd).padding(end = 2.dp))
        }

        Text(
            text = "%.1fs".format(durationSec),
            color = Color.White,
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(3.dp)
                .background(Color(0x88000000), RoundedCornerShape(3.dp))
                .padding(horizontal = 3.dp)
        )
    }
}

@Composable
private fun TrimHandle(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .width(6.dp)
            .height(40.dp)
            .clip(RoundedCornerShape(3.dp))
            .background(Orange)
    )
}

@Composable
private fun TimelineCursor() {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Box(
            modifier = Modifier
                .width(2.dp)
                .fillMaxHeight()
                .background(Color.White)
        )
    }
}

@Composable
private fun AudioWaveformStrip(modifier: Modifier = Modifier) {
    Row(
        modifier = modifier
            .clip(RoundedCornerShape(4.dp))
            .background(Color(0xFF1A2A1A)),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(1.dp)
    ) {
        repeat(60) { i ->
            val h = (4 + (Math.sin(i * 0.5) * 6 + Math.sin(i * 0.3) * 4)).toFloat().coerceAtLeast(2f)
            Box(
                modifier = Modifier
                    .width(2.dp)
                    .height(h.dp)
                    .background(Color(0xFF44AA44))
            )
        }
    }
}
