package com.sdk.video.sample.feature.editor

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
import com.sdk.video.sample.core.model.ClipItem
import com.sdk.video.sample.core.model.TextClipState
import com.sdk.video.sample.core.model.TimelineViewModel
import com.sdk.video.timeline.TimelineManager
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

private val Orange = Color(0xFFFF6B00)
private val Accent = Color(0xFF00D7FF)

/**
 * 剪映式多轨道剪辑时间线组件 - Feature Editor/Timeline View
 * 
 * 核心逻辑与功能:
 * 1. 播放位置自适应同步：当视频播放时，计算 positionUs 并通过 ScrollState 将轨道视图在横轴方向以 60dp/s 的比例自动平滑向右滚动。
 * 2. 拖拽定位 (Scrubbing)：当用户在非播放状态下主动划动时间线，反向计算划动百分比换算为微秒 (Us) 并传递给播放器 [TimelinePlayer.seekTo]，呈现极低延迟的预览取帧。
 * 3. 轨道图层分布：
 *    - 标尺轨道 (TimeRuler)：利用 [Canvas] 动态绘制时间码和刻度线。
 *    - 视频轨道 (VideoTrack)：绘制视频块、选中黄色包络线、裁剪拖拽柄 (TrimHandle)、以及转场连接节点 (Transition Nodes)。
 *    - 字幕轨道 (SubtitleTrack)：绘制亮青色描边的半透明字幕带。
 *    - 音频波形轨 (AudioTrack)：利用正弦函数生成高仿真音频模拟波形，突显专业感。
 */
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

    // 缩放尺寸换算：每秒视频对应 60.dp 宽度
    val dpPerSecond = 60f
    val dpPerUs = dpPerSecond / 1_000_000f

    val scrollState = rememberScrollState()

    // 播放时自动同步滚动到播放头对齐位置
    val isPlaying by timelineVm.player.isPlaying.collectAsState()
    LaunchedEffect(positionUs) {
        if (isPlaying) {
            val targetScrollDp = (positionUs * dpPerUs).dp - playheadOffset
            val targetScrollPx = with(density) { targetScrollDp.toPx().toInt() }
            scrollState.scrollTo(targetScrollPx.coerceAtLeast(0))
        }
    }

    // 拖拽时间线时，反向更新播放器指针位置
    LaunchedEffect(scrollState.value) {
        if (scrollState.isScrollInProgress && !isPlaying) {
            val scrollDp = with(density) { scrollState.value.toDp() }
            val targetUs = (((scrollDp + playheadOffset).value / dpPerUs)).toLong()
            timelineVm.player.seekTo(targetUs.coerceIn(0L, totalDurationUs))
        }
    }

    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(Color(0xFF0D0D0F))
            .border(BorderStroke(1.dp, Color(0xFF1B1B1F)))
    ) {
        // 多轨道头部标识区
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
                .height(200.dp)
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
                Row(
                    modifier = Modifier
                        .fillMaxSize()
                        .horizontalScroll(scrollState)
                ) {
                    // 起始空白占位，确保播放头对准时间线 0 秒刻度
                    Spacer(modifier = Modifier.width(playheadOffset))

                    // 堆叠的音视频多轨道列表
                    Column(
                        modifier = Modifier
                            .width((totalDurationUs * dpPerUs).dp)
                            .fillMaxHeight()
                    ) {
                        // 1. 时间码标尺
                        TimeRuler(
                            totalDurationUs = totalDurationUs,
                            dpPerUs = dpPerUs,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(26.dp)
                        )

                        Spacer(Modifier.height(6.dp))

                        // 2. 核心视频轨道 (含关键帧红钻石与转场节点)
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

                        // 3. 字幕轨道
                        SubtitleTrack(
                            textClips = textClips,
                            dpPerUs = dpPerUs,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(32.dp)
                        )

                        Spacer(Modifier.height(6.dp))

                        // 4. 音频波形轨道
                        AudioTrack(
                            totalDurationUs = totalDurationUs,
                            dpPerUs = dpPerUs,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(36.dp)
                        )
                    }

                    // 结束空白占位，使得末尾帧可以滚动至播放头
                    Spacer(modifier = Modifier.width(screenWidth - playheadOffset))
                }

                // 悬浮不动的播放头十字红线
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

        drawLine(
            color = Color(0xFF222226),
            start = androidx.compose.ui.geometry.Offset(0f, size.height),
            end = androidx.compose.ui.geometry.Offset(size.width, size.height),
            strokeWidth = 1.dp.toPx()
        )

        val paint = android.graphics.Paint().apply {
            color = android.graphics.Color.parseColor("#888892")
            textSize = 9.dp.toPx()
            typeface = android.graphics.Typeface.create(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD)
        }

        for (sec in 0..totalSec + 1) {
            val xSec = sec * pxPerSec

            if (xSec < size.width) {
                // 主刻度线
                drawLine(
                    color = Color(0xFF44444B),
                    start = androidx.compose.ui.geometry.Offset(xSec, size.height - 8.dp.toPx()),
                    end = androidx.compose.ui.geometry.Offset(xSec, size.height),
                    strokeWidth = 1.2.dp.toPx()
                )

                // 绘制时间文本 "00:02"
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

            // 细分微刻度 (每秒 5 个格)
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

        // 覆盖在视频片段边缘交汇处的转场按钮
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
                        // Demo 支持循环切换几种内置典型转场特效
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
        // 后台异步解码视频帧并呈现在轨道缩略图上
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

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(if (isSelected) 0x1AFF6B00 else 0x44000000))
        )

        // 选中时显示左右可拖曳手柄样式
        if (isSelected) {
            TrimHandle(Modifier.align(Alignment.CenterStart))
            TrimHandle(Modifier.align(Alignment.CenterEnd))
        }

        // 时长角标
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

        // 关键帧红钻石渲染
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
                        .background(Color(0x3300D7FF)) // 高级感青色半透明
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
            .background(Color(0xFF14241B)) // 暗色调绿色背景，对标专业音频轨
            .border(1.dp, Color(0xFF20482B), RoundedCornerShape(4.dp))
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val pxWidth = size.width
            val stepPx = 4.dp.toPx()
            val waveCount = (pxWidth / stepPx).toInt()

            for (i in 0..waveCount) {
                val x = i * stepPx
                val phase = i * 0.15f
                val h = size.height * 0.7f * (0.3f + 0.5f * abs(sin(phase) * cos(phase * 0.6f + 0.2f)))

                drawLine(
                    color = Color(0xFF00FF88), // 亮绿色音频波
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

        Box(
            modifier = Modifier
                .size(12.dp)
                .rotate(45f)
                .background(Orange)
                .border(1.dp, Color.White, RoundedCornerShape(1.dp))
        )
    }
}
