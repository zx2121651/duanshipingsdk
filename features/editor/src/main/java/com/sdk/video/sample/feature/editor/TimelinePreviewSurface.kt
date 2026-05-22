@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.sample.feature.editor

import android.net.Uri
import android.opengl.GLSurfaceView
import androidx.compose.foundation.BorderStroke
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
import com.sdk.video.sample.core.model.TimelinePlayer
import com.sdk.video.timeline.TimelinePreviewBridge
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

private val Orange = Color(0xFFFF6B00)
private val DarkBg = Color(0xFF0D0D0F)
private val BorderColor = Color(0xFF222226)

/**
 * 视频编辑时钟与 OpenGL ES 渲染视窗 - Feature Editor/Preview Component
 * 
 * 核心逻辑与功能:
 * 1. OpenGL ES 渲染绑定：利用 [AndroidView] 包装传统的 Android 原生 [GLSurfaceView]。
 * 2. C++ 桥接与渲染循环：
 *    - 注册 [TimelinePreviewBridge] 在 OpenGL 线程初始化 (EGL Version 3)。
 *    - 在 [GLSurfaceView.Renderer.onDrawFrame] 周期中，通过 NDK 向 C++ 底层解码渲染引擎请求绘制对应纳秒级的渲染帧 [TimelinePreviewBridge.renderFrame]。
 * 3. 素材动态热插拔：在 [LaunchedEffect(clips)] 中对选中的 Uri 进行动态加载与底层解码器池的关联。
 * 4. 关键帧 (Keyframe) 控制器：提供一键为当前微秒时间戳 (positionUs) 插入/移除音视频参数关键帧的钻石操作块。
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

    // 动态在底层引擎注册并开启当前的片段流
    LaunchedEffect(clips) {
        clips.forEach { (clipId, uri) ->
            bridge.registerClip(clipId, uri.toString())
        }
    }

    // 播放时间点变动时，换算微秒 (Us) 转换为纳秒 (Ns) 并更新 C++ 渲染参数
    LaunchedEffect(positionUs) {
        positionNsRef.longValue = positionUs * 1_000L
    }

    Column(
        modifier = modifier
            .background(DarkBg)
    ) {
        // 1. 带圆角边框的高端预览显示区 (GLSurfaceView 宿主)
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
                        // 设置 OpenGL ES 3.0 上下文环境
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
                        // 设为连续渲染模式以匹配播放时钟帧率
                        glView.renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
                    }
                },
                update = { glView ->
                    if (glView.renderMode == GLSurfaceView.RENDERMODE_WHEN_DIRTY) {
                        glView.requestRender()
                    }
                },
                onRelease = { _ ->
                    // 销毁 GL 环境时，释放在 native 线程持有的着色器与纹理句柄
                    bridge.releaseOnGLThread()
                }
            )
        }

        // 2. 时间码控制栏 + 播放/暂停/快进/快退/关键帧操作块
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
        // 对标剪映的极细时间拖动条
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

        Row(
            modifier            = Modifier.fillMaxWidth(),
            verticalAlignment   = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            // 当前微秒位置换算的时间字符串
            Text(
                text     = formatTime(positionUs),
                color    = Color(0xFFCCCCCC),
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold
            )

            // 中央媒体控制器组
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

            // 右侧关键帧管理区
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
