@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.sample.ui

import android.net.Uri
import android.opengl.GLSurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.sdk.video.timeline.TimelinePreviewBridge
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

private val Orange = Color(0xFFFF6B00)
private val DarkBg = Color(0xFF0D0D0F)
private val BorderColor = Color(0xFF222226)

/**
 * TimelinePreviewSurface
 *
 * @param timelineHandle  来自 [TimelineManager.getNativeHandle] 的 C++ native 句柄
 * @param clips           当前时间线上的素材列表，用于向 DecoderPool 注册媒体
 * @param player          播放时钟，驱动 [positionUs]
 * @param hasKeyframeAtCurrent 当前播放头位置是否存在关键帧
 * @param onToggleKeyframe 点击添加/移除关键帧回调
 * @param modifier        外部 modifier
 */
@Composable
fun TimelinePreviewSurface(
    timelineHandle: Long,
    clips: List<Pair<String, Uri>>,
    player: TimelinePlayer,
    hasKeyframeAtCurrent: Boolean,
    onToggleKeyframe: () -> Unit,
    modifier: Modifier = Modifier
) {
    val positionUs  by player.positionUs.collectAsState()
    val isPlaying   by player.isPlaying.collectAsState()
    val totalDurUs  by player.totalDurationUs.collectAsState()

    val bridge = remember { TimelinePreviewBridge() }
    val positionNsRef = remember { androidx.compose.runtime.mutableLongStateOf(0L) }

    LaunchedEffect(clips) {
        clips.forEach { (clipId, uri) ->
            bridge.registerClip(clipId, uri.toString())
        }
    }

    LaunchedEffect(positionUs) {
        positionNsRef.longValue = positionUs * 1_000L
    }

    Column(
        modifier = modifier
            .background(DarkBg)
    ) {
        // ── Video surface with premium bezel frame ───────────────────────────
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(12.dp)
                .clip(RoundedCornerShape(16.dp))
                .background(Color.Black)
                .border(1.2.dp, BorderColor, RoundedCornerShape(16.dp)),
            contentAlignment = Alignment.Center
        ) {
            AndroidView(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(16f / 9f)
                    .clip(RoundedCornerShape(12.dp)),
                factory = { ctx ->
                    GLSurfaceView(ctx).also { glView ->
                        glView.setEGLContextClientVersion(3)
                        glView.setRenderer(object : GLSurfaceView.Renderer {
                            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                                bridge.initOnGLThread(
                                    timelineHandle,
                                    glView.width.coerceAtLeast(1),
                                    glView.height.coerceAtLeast(1)
                                )
                            }

                            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                                bridge.surfaceChanged(width, height)
                            }

                            override fun onDrawFrame(gl: GL10?) {
                                bridge.renderFrame(positionNsRef.longValue)
                            }
                        })
                        glView.renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
                    }
                },
                update = { glView ->
                    if (glView.renderMode == GLSurfaceView.RENDERMODE_WHEN_DIRTY) {
                        glView.requestRender()
                    }
                },
                onRelease = { _ ->
                    bridge.releaseOnGLThread()
                }
            )
        }

        // ── Playback controls ────────────────────────────────────────────────
        PreviewPlaybackControls(
            isPlaying            = isPlaying,
            positionUs           = positionUs,
            totalDurUs           = totalDurUs,
            hasKeyframeAtCurrent = hasKeyframeAtCurrent,
            onToggleKeyframe     = onToggleKeyframe,
            onPlayPause          = { player.togglePlayPause() },
            onSeekTo             = { player.seekTo(it) },
            onSkipStart          = { player.reset() },
            onSkipEnd            = { player.seekTo(totalDurUs) }
        )
    }
}

@Composable
private fun PreviewPlaybackControls(
    isPlaying:   Boolean,
    positionUs:  Long,
    totalDurUs:  Long,
    hasKeyframeAtCurrent: Boolean,
    onToggleKeyframe: () -> Unit,
    onPlayPause: () -> Unit,
    onSeekTo:    (Long) -> Unit,
    onSkipStart: () -> Unit,
    onSkipEnd:   () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF0F0F12))
            .border(BorderStroke(1.dp, Color(0xFF1B1B1F)), RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp))
            .padding(horizontal = 16.dp, vertical = 8.dp)
    ) {
        // Progress slider styled like Jianying
        val sliderProgress = if (totalDurUs > 0) positionUs.toFloat() / totalDurUs else 0f
        Slider(
            value         = sliderProgress,
            onValueChange = { frac -> onSeekTo((frac * totalDurUs).toLong()) },
            colors        = SliderDefaults.colors(
                thumbColor        = Color.White,
                activeTrackColor  = Orange,
                inactiveTrackColor = Color(0xFF282830)
            ),
            modifier = Modifier
                .fillMaxWidth()
                .height(18.dp)
        )

        Spacer(Modifier.height(4.dp))

        // Time labels + transport buttons
        Row(
            modifier            = Modifier.fillMaxWidth(),
            verticalAlignment   = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text     = formatTime(positionUs),
                color    = Color(0xFFCCCCCC),
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold
            )

            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment     = Alignment.CenterVertically
            ) {
                IconButton(onClick = onSkipStart, modifier = Modifier.size(36.dp)) {
                    Icon(Icons.Filled.SkipPrevious, null, tint = Color.White, modifier = Modifier.size(22.dp))
                }
                IconButton(
                    onClick  = onPlayPause,
                    modifier = Modifier
                        .size(44.dp)
                        .clip(CircleShape)
                        .background(
                            Brush.radialGradient(
                                colors = listOf(Orange, Color(0xFFFF4500))
                            )
                        )
                ) {
                    Icon(
                        imageVector  = if (isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                        contentDescription = if (isPlaying) "暂停" else "播放",
                        tint         = Color.White,
                        modifier     = Modifier.size(24.dp)
                    )
                }
                IconButton(onClick = onSkipEnd, modifier = Modifier.size(36.dp)) {
                    Icon(Icons.Filled.SkipNext, null, tint = Color.White, modifier = Modifier.size(22.dp))
                }
            }

            // Keyframe diamond action button on the right side
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Row(
                    modifier = Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF222228))
                        .border(1.dp, Color(0xFF33333E), RoundedCornerShape(8.dp))
                        .clickable(onClick = onToggleKeyframe)
                        .padding(horizontal = 10.dp, vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box(
                        modifier = Modifier
                            .size(10.dp)
                            .rotate(45f)
                            .background(if (hasKeyframeAtCurrent) Orange else Color.White)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = if (hasKeyframeAtCurrent) "◆-" else "◆+",
                        color = if (hasKeyframeAtCurrent) Orange else Color.White,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
                
                Text(
                    text     = formatTime(totalDurUs),
                    color    = Color(0xFF666672),
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Medium
                )
            }
        }
    }
}

private fun formatTime(us: Long): String {
    val totalSec = us / 1_000_000L
    val min      = totalSec / 60
    val sec      = totalSec % 60
    val tenths   = (us % 1_000_000L) / 100_000L
    return "%02d:%02d.%d".format(min, sec, tenths)
}
