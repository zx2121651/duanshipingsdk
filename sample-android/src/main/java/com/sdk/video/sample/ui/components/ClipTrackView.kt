package com.sdk.video.sample.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.Title
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import coil.request.ImageRequest
import coil.size.Size
import coil.decode.VideoFrameDecoder
import com.sdk.video.sample.state.ClipItem
import com.sdk.video.sample.state.TextClipState
import com.sdk.video.sample.state.TimelineViewModel
import com.sdk.video.timeline.TimelineManager

private val Orange = Color(0xFFFF6B00)
private val Accent = Color(0xFF00D7FF)

@Composable
fun ClipTrackView(
    timelineVm: TimelineViewModel,
    selectedIndex: Int,
    onSelectClip: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val clips by timelineVm.clips.collectAsState()
    val totalDurationUs by timelineVm.totalDurationUs.collectAsState()
    val positionUs by timelineVm.player.positionUs.collectAsState()
    val keyframes by timelineVm.keyframes.collectAsState()
    val textClips by timelineVm.textClips.collectAsState()

    val density = LocalDensity.current
    val screenWidth = LocalConfiguration.current.screenWidthDp.dp
    val playheadOffset = 160.dp

    // Scale: 60 dp per second
    val dpPerSecond = 60f
    val dpPerUs = dpPerSecond / 1_000_000f

    val scrollState = rememberScrollState()

    // Sync scroll with player position
    val isPlaying by timelineVm.player.isPlaying.collectAsState()
    LaunchedEffect(positionUs) {
        if (isPlaying) {
            val targetScrollDp = (positionUs * dpPerUs).dp - playheadOffset
            val targetScrollPx = with(density) { targetScrollDp.toPx().toInt() }
            scrollState.scrollTo(targetScrollPx.coerceAtLeast(0))
        }
    }

    // Sync player position with user scroll
    LaunchedEffect(scrollState.value) {
        if (scrollState.isScrollInProgress && !isPlaying) {
            val scrollDp = with(density) { scrollState.value.toDp() }
            val targetUs = ((scrollDp + playheadOffset) / dpPerUs).toLong()
            timelineVm.player.seekTo(targetUs.coerceIn(0L, totalDurationUs))
        }
    }

    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(Color(0xFF0D0D0F))
            .border(BorderStroke(1.dp, Color(0xFF1B1B1F)))
    ) {
        // Track labels row
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "多轨道剪辑时间线 (Jianying Timeline)",
                color = Color(0xFF888892),
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold
            )
            if (clips.isNotEmpty()) {
                Text(
                    text = "左右滑动调整播放位置",
                    color = Accent.copy(alpha = 0.8f),
                    fontSize = 10.sp
                )
            }
        }

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(200.dp) // Total timeline area height
        ) {
            if (clips.isEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(horizontal = 16.dp, vertical = 8.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color(0xFF141416))
                        .border(1.dp, Color(0xFF222226), RoundedCornerShape(12.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Text("暂无媒体片段，请从首页导入视频", color = Color(0xFF55555C), fontSize = 12.sp)
                }
            } else {
                // Scrollable container
                Row(
                    modifier = Modifier
                        .fillMaxSize()
                        .horizontalScroll(scrollState)
                ) {
                    // Start spacer padding to allow the playhead to align with time 0
                    Spacer(modifier = Modifier.width(playheadOffset))

                    // Timeline tracks container
                    Column(
                        modifier = Modifier
                            .width((totalDurationUs * dpPerUs).dp)
                            .fillMaxHeight()
                    ) {
                        // 1. Timecode Ruler
                        TimeRuler(
                            totalDurationUs = totalDurationUs,
                            dpPerUs = dpPerUs,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(26.dp)
                        )

                        Spacer(Modifier.height(6.dp))

                        // 2. Video Track (with Transition nodes and keyframes)
                        VideoTrack(
                            clips = clips,
                            selectedIndex = selectedIndex,
                            onSelectClip = onSelectClip,
                            keyframes = keyframes,
                            dpPerUs = dpPerUs,
                            timelineVm = timelineVm,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(64.dp)
                        )

                        Spacer(Modifier.height(6.dp))

                        // 3. Subtitle / Text Track
                        SubtitleTrack(
                            textClips = textClips,
                            dpPerUs = dpPerUs,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(32.dp)
                        )

                        Spacer(Modifier.height(6.dp))

                        // 4. Audio Track
                        AudioTrack(
                            totalDurationUs = totalDurationUs,
                            dpPerUs = dpPerUs,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(36.dp)
                        )
                    }

                    // End spacer padding so playhead can reach the end
                    Spacer(modifier = Modifier.width(screenWidth - playheadOffset))
                }

                // Fixed Playhead Cursor Overlay
                PlayheadCursor(
                    offset = playheadOffset,
                    modifier = Modifier.fillMaxHeight()
                )
            }
        }
    }
}

@Composable
private fun TimeRuler(
    totalDurationUs: Long,
    dpPerUs: Float,
    modifier: Modifier = Modifier
) {
    Canvas(modifier = modifier) {
        val totalSec = (totalDurationUs / 1_000_000f).toInt()
        val pxPerSec = 60.dp.toPx()

        // Bottom border line
        drawLine(
            color = Color(0xFF222226),
            start = androidx.compose.ui.geometry.Offset(0f, size.height),
            end = androidx.compose.ui.geometry.Offset(size.width, size.height),
            strokeWidth = 1.dp.toPx()
        )

        // Draw ticks
        val paint = android.graphics.Paint().apply {
            color = android.graphics.Color.parseColor("#888892")
            textSize = 9.dp.toPx()
            typeface = android.graphics.Typeface.create(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD)
        }

        for (sec in 0..totalSec + 1) {
            val xSec = sec * pxPerSec

            if (xSec < size.width) {
                // Major tick
                drawLine(
                    color = Color(0xFF44444B),
                    start = androidx.compose.ui.geometry.Offset(xSec, size.height - 8.dp.toPx()),
                    end = androidx.compose.ui.geometry.Offset(xSec, size.height),
                    strokeWidth = 1.2.dp.toPx()
                )

                // Text Label e.g. "00:02"
                val min = sec / 60
                val s = sec % 60
                val timeStr = "%02d:%02d".format(min, s)
                drawContext.canvas.nativeCanvas.drawText(
                    timeStr,
                    xSec + 4.dp.toPx(),
                    size.height - 10.dp.toPx(),
                    paint
                )
            }

            // Minor ticks (5 ticks per second, i.e., every 0.2s)
            for (j in 1..4) {
                val xMinor = xSec + j * (pxPerSec / 5f)
                if (xMinor < size.width) {
                    drawLine(
                        color = Color(0xFF26262B),
                        start = androidx.compose.ui.geometry.Offset(xMinor, size.height - 4.dp.toPx()),
                        end = androidx.compose.ui.geometry.Offset(xMinor, size.height),
                        strokeWidth = 0.8.dp.toPx()
                    )
                }
            }
        }
    }
}

@Composable
private fun VideoTrack(
    clips: List<ClipItem>,
    selectedIndex: Int,
    onSelectClip: (Int) -> Unit,
    keyframes: List<Long>,
    dpPerUs: Float,
    timelineVm: TimelineViewModel,
    modifier: Modifier = Modifier
) {
    Box(modifier = modifier) {
        // 1. Clips Row
        Row(modifier = Modifier.fillMaxSize()) {
            var clipStartUs = 0L
            clips.forEachIndexed { index, clip ->
                val widthDp = (clip.durationUs * dpPerUs).dp
                ClipCell(
                    clip = clip,
                    isSelected = index == selectedIndex,
                    widthDp = widthDp,
                    keyframes = keyframes,
                    clipStartUs = clipStartUs,
                    dpPerUs = dpPerUs,
                    onClick = { onSelectClip(index) }
                )
                clipStartUs += clip.durationUs
            }
        }

        // 2. Overlaid Transition Nodes
        var accumulatedUs = 0L
        for (i in 0 until clips.size - 1) {
            accumulatedUs += clips[i].durationUs
            val boundaryDp = (accumulatedUs * dpPerUs).dp

            Box(
                modifier = Modifier
                    .offset(x = boundaryDp - 10.dp)
                    .align(Alignment.CenterStart)
                    .size(20.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(Color(0xFF202025))
                    .border(1.dp, Color(0xFF40404C), RoundedCornerShape(4.dp))
                    .clickable {
                        val nextTrans = when (i % 4) {
                            0 -> TimelineManager.TransitionType.CROSSFADE
                            1 -> TimelineManager.TransitionType.FLASH
                            2 -> TimelineManager.TransitionType.ZOOM_IN
                            else -> TimelineManager.TransitionType.NONE
                        }
                        timelineVm.setTransition(nextTrans, 500_000L)
                    },
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Filled.Layers,
                    contentDescription = "转场",
                    tint = if (i % 2 == 0) Orange else Color.White,
                    modifier = Modifier.size(12.dp)
                )
            }
        }
    }
}

@Composable
private fun ClipCell(
    clip: ClipItem,
    isSelected: Boolean,
    widthDp: androidx.compose.ui.unit.Dp,
    keyframes: List<Long>,
    clipStartUs: Long,
    dpPerUs: Float,
    onClick: () -> Unit
) {
    val context = LocalContext.current
    val durationSec = (clip.durationUs / 1_000_000f)

    Box(
        modifier = Modifier
            .width(widthDp)
            .fillMaxHeight()
            .clickable(onClick = onClick)
            .then(
                if (isSelected) Modifier.border(2.dp, Orange, RoundedCornerShape(4.dp))
                else Modifier.border(1.dp, Color(0xFF222226), RoundedCornerShape(4.dp))
            )
            .clip(RoundedCornerShape(4.dp))
    ) {
        // Thumbnail image
        AsyncImage(
            model = ImageRequest.Builder(context)
                .data(clip.uri)
                .decoderFactory(VideoFrameDecoder.Factory())
                .size(160, 120)
                .crossfade(false)
                .build(),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxSize()
        )

        // Overlay with subtle selection color
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(if (isSelected) 0x1AFF6B00 else 0x44000000))
        )

        // Trim handles if selected
        if (isSelected) {
            TrimHandle(Modifier.align(Alignment.CenterStart))
            TrimHandle(Modifier.align(Alignment.CenterEnd))
        }

        // Clip duration text
        Text(
            text = "%.1fs".format(durationSec),
            color = Color.White,
            fontSize = 9.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(2.dp)
                .background(Color(0xAA000000), RoundedCornerShape(2.dp))
                .padding(horizontal = 3.dp, vertical = 1.dp)
        )

        // Keyframe overlays inside the clip
        val clipEndUs = clipStartUs + clip.durationUs
        keyframes.forEach { kfUs ->
            if (kfUs in clipStartUs..clipEndUs) {
                val relUs = kfUs - clipStartUs
                val xOffsetDp = (relUs * dpPerUs).dp
                Box(
                    modifier = Modifier
                        .offset(x = xOffsetDp - 5.dp)
                        .align(Alignment.CenterStart)
                        .size(10.dp)
                        .rotate(45f)
                        .background(Orange)
                        .border(1.dp, Color.White, RoundedCornerShape(1.dp))
                )
            }
        }
    }
}

@Composable
private fun TrimHandle(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .width(8.dp)
            .fillMaxHeight()
            .background(Orange),
        contentAlignment = Alignment.Center
    ) {
        // Draw three white vertical lines inside handle (drag texture)
        Column(
            verticalArrangement = Arrangement.spacedBy(2.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            repeat(3) {
                Box(
                    modifier = Modifier
                        .size(width = 1.5.dp, height = 8.dp)
                        .background(Color.White)
                )
            }
        }
    }
}

@Composable
private fun SubtitleTrack(
    textClips: List<TextClipState>,
    dpPerUs: Float,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .background(Color(0xFF101012))
    ) {
        if (textClips.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .border(1.dp, Color(0xFF1B1B1F), RoundedCornerShape(4.dp)),
                contentAlignment = Alignment.CenterStart
            ) {
                Text("文字轨道 (无字幕)", color = Color(0xFF44444B), fontSize = 10.sp, modifier = Modifier.padding(start = 8.dp))
            }
        } else {
            textClips.forEach { subtitle ->
                val startDp = (subtitle.startUs * dpPerUs).dp
                val widthDp = ((subtitle.endUs - subtitle.startUs) * dpPerUs).dp

                Box(
                    modifier = Modifier
                        .offset(x = startDp)
                        .width(widthDp)
                        .fillMaxHeight()
                        .clip(RoundedCornerShape(4.dp))
                        .background(Color(0x3300D7FF)) // transparent cyan glow
                        .border(1.dp, Accent, RoundedCornerShape(4.dp))
                        .padding(horizontal = 4.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(2.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Filled.Title,
                            contentDescription = null,
                            tint = Accent,
                            modifier = Modifier.size(10.dp)
                        )
                        Text(
                            text = subtitle.text,
                            color = Color.White,
                            fontSize = 9.sp,
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
private fun AudioTrack(
    totalDurationUs: Long,
    dpPerUs: Float,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(4.dp))
            .background(Color(0xFF14241B)) // Deep dark green hue
            .border(1.dp, Color(0xFF20482B), RoundedCornerShape(4.dp))
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val pxWidth = size.width
            val stepPx = 4.dp.toPx()
            val waveCount = (pxWidth / stepPx).toInt()

            // Generate deterministic waveform peaks
            for (i in 0..waveCount) {
                val x = i * stepPx
                val phase = i * 0.15f
                val h = (size.height * 0.7f * (0.3f + 0.5f * Math.abs(Math.sin(phase.toDouble()) * Math.cos(phase * 0.6f + 0.2f)))).toFloat()

                // Draw wave line centered vertically
                drawLine(
                    color = Color(0xFF00FF88),
                    start = androidx.compose.ui.geometry.Offset(x, (size.height - h) / 2f),
                    end = androidx.compose.ui.geometry.Offset(x, (size.height + h) / 2f),
                    strokeWidth = 2.dp.toPx()
                )
            }
        }

        Text(
            text = "原声 / 导入音频",
            color = Color(0xFF88AA88),
            fontSize = 9.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(start = 8.dp, top = 2.dp)
        )
    }
}

@Composable
private fun PlayheadCursor(
    offset: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxHeight()
            .width(20.dp)
            .offset(x = offset - 10.dp),
        contentAlignment = Alignment.TopCenter
    ) {
        // Playhead line
        Box(
            modifier = Modifier
                .width(2.dp)
                .fillMaxHeight()
                .background(
                    Brush.verticalGradient(
                        colors = listOf(Orange, Orange.copy(alpha = 0.5f), Color.Transparent)
                    )
                )
        )

        // Playhead head
        Box(
            modifier = Modifier
                .size(12.dp)
                .rotate(45f)
                .background(Orange)
                .border(1.dp, Color.White, RoundedCornerShape(1.dp))
        )
    }
}
