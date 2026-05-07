@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.sample.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sdk.video.VideoFilterManager

/**
 * 性能浮层（Avg/P50/P90/P99/Dropped）— 拍摄页与编辑页共用。
 */
@Composable
fun PerfOverlay(filterManager: VideoFilterManager,
                 modifier: Modifier = Modifier) {
    val metrics by filterManager.performanceMetrics.collectAsState()
    val avg = metrics?.averageFrameTimeMs ?: 0f
    val p50 = metrics?.p50FrameTimeMs ?: 0f
    val p90 = metrics?.p90FrameTimeMs ?: 0f
    val p99 = metrics?.p99FrameTimeMs ?: 0f
    val dropped = metrics?.droppedFrames ?: 0

    Column(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0x88000000))
            .padding(horizontal = 10.dp, vertical = 6.dp)
    ) {
        Text(
            text = "Avg ${"%.1f".format(avg)} ms",
            color = if (avg > 16f) Color(0xFFFF6B6B) else Color(0xFF4ADE80),
            fontSize = 12.sp
        )
        Text("P50 ${"%.1f".format(p50)} ms", color = Color.White, fontSize = 11.sp)
        Text("P90 ${"%.1f".format(p90)} ms", color = Color.White, fontSize = 11.sp)
        Text("P99 ${"%.1f".format(p99)} ms", color = Color.White, fontSize = 11.sp)
        Text(
            text = "Drop $dropped",
            color = if (dropped > 0) Color(0xFFFF6B6B) else Color.White,
            fontSize = 11.sp
        )
    }
}
