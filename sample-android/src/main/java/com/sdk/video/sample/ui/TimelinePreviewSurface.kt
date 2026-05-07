@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.sample.ui

import android.net.Uri
import android.opengl.GLSurfaceView
import androidx.compose.foundation.background
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.sdk.video.timeline.TimelinePreviewBridge
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

private val Orange = Color(0xFFFF6B00)

/**
 * TimelinePreviewSurface
 *
 * Composable 预览区：用 [AndroidView] 包装 [GLSurfaceView]，
 * 在 GL 线程通过 [TimelinePreviewBridge] 调用 `Compositor::renderFrameAtTime`，
 * 实现时间线当前帧的实时渲染。
 *
 * @param timelineHandle  来自 [TimelineManager.getNativeHandle] 的 C++ native 句柄
 * @param clips           当前时间线上的素材列表，用于向 DecoderPool 注册媒体
 * @param player          播放时钟，驱动 [positionUs]
 * @param modifier        外部 modifier
 */
@Composable
fun TimelinePreviewSurface(
    timelineHandle: Long,
    clips: List<Pair<String, Uri>>,
    player: TimelinePlayer,
    modifier: Modifier = Modifier
) {
    val positionUs  by player.positionUs.collectAsState()
    val isPlaying   by player.isPlaying.collectAsState()
    val totalDurUs  by player.totalDurationUs.collectAsState()

    // Keep a stable reference to the bridge so it's not recreated on recomposition.
    val bridge = remember { TimelinePreviewBridge() }
    // Shared mutable state for position flowing into the GL thread.
    val positionNsRef = remember { androidx.compose.runtime.mutableLongStateOf(0L) }

    // Re-register clips with the native DecoderPool whenever the clip list changes.
    LaunchedEffect(clips) {
        clips.forEach { (clipId, uri) ->
            bridge.registerClip(clipId, uri.toString())
        }
    }

    // Sync positionUs → positionNsRef (ns) for the renderer
    LaunchedEffect(positionUs) {
        positionNsRef.longValue = positionUs * 1_000L
    }

    Column(modifier = modifier) {
        // ── Video surface ────────────────────────────────────────────────────
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(Color.Black),
            contentAlignment = Alignment.Center
        ) {
            AndroidView(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(16f / 9f)
                    .clip(RoundedCornerShape(8.dp)),
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
                    // Trigger redraw whenever position changes
                    if (glView.renderMode == GLSurfaceView.RENDERMODE_WHEN_DIRTY) {
                        glView.requestRender()
                    }
                },
                onRelease = { _ ->
                    // GLSurfaceView destructor triggers onSurfaceDestroyed → but bridge
                    // release must be explicit here since Compose may not call it in time.
                    bridge.releaseOnGLThread()
                }
            )
        }

        // ── Playback controls ────────────────────────────────────────────────
        PreviewPlaybackControls(
            isPlaying    = isPlaying,
            positionUs   = positionUs,
            totalDurUs   = totalDurUs,
            onPlayPause  = { player.togglePlayPause() },
            onSeekTo     = { player.seekTo(it) },
            onSkipStart  = { player.reset() },
            onSkipEnd    = { player.seekTo(totalDurUs) }
        )
    }
}

// ---------------------------------------------------------------------------
// PlaybackControls
// ---------------------------------------------------------------------------
@Composable
private fun PreviewPlaybackControls(
    isPlaying:   Boolean,
    positionUs:  Long,
    totalDurUs:  Long,
    onPlayPause: () -> Unit,
    onSeekTo:    (Long) -> Unit,
    onSkipStart: () -> Unit,
    onSkipEnd:   () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF0D0D0D))
            .padding(horizontal = 12.dp, vertical = 4.dp)
    ) {
        // Progress slider
        val sliderProgress = if (totalDurUs > 0) positionUs.toFloat() / totalDurUs else 0f
        Slider(
            value         = sliderProgress,
            onValueChange = { frac -> onSeekTo((frac * totalDurUs).toLong()) },
            colors        = SliderDefaults.colors(
                thumbColor        = Orange,
                activeTrackColor  = Orange,
                inactiveTrackColor = Color(0xFF444444)
            ),
            modifier = Modifier.fillMaxWidth()
        )

        // Time labels + transport buttons
        Row(
            modifier            = Modifier.fillMaxWidth(),
            verticalAlignment   = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text     = formatTime(positionUs),
                color    = Color(0xFFAAAAAA),
                fontSize = 11.sp
            )

            Row(
                horizontalArrangement = Arrangement.spacedBy(4.dp),
                verticalAlignment     = Alignment.CenterVertically
            ) {
                IconButton(onClick = onSkipStart, modifier = Modifier.size(32.dp)) {
                    Icon(Icons.Filled.SkipPrevious, null, tint = Color.White)
                }
                IconButton(
                    onClick  = onPlayPause,
                    modifier = Modifier
                        .size(40.dp)
                        .clip(CircleShape)
                        .background(Orange)
                ) {
                    Icon(
                        imageVector  = if (isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                        contentDescription = if (isPlaying) "暂停" else "播放",
                        tint         = Color.White,
                        modifier     = Modifier.size(24.dp)
                    )
                }
                IconButton(onClick = onSkipEnd, modifier = Modifier.size(32.dp)) {
                    Icon(Icons.Filled.SkipNext, null, tint = Color.White)
                }
            }

            Text(
                text     = formatTime(totalDurUs),
                color    = Color(0xFF666666),
                fontSize = 11.sp
            )
        }
    }
}

private fun formatTime(us: Long): String {
    val totalSec = us / 1_000_000L
    val min      = totalSec / 60
    val sec      = totalSec % 60
    val tenths   = (us % 1_000_000L) / 100_000L
    return "%d:%02d.%d".format(min, sec, tenths)
}
