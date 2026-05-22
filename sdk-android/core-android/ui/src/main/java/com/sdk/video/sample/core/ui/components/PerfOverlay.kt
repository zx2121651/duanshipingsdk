@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.sample.core.ui.components

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
20:  * 性能监测浮层组件 (Average, P50, P90, P99 Frame Time & Dropped Frames)
21:  * 
22:  * 设计目的:
23:  * 1. 实时展示视频滤镜/特效渲染引擎的帧耗时与丢帧率。
24:  * 2. 在拍摄预览 (CaptureScreen) 和滤镜编辑预览 (EditScreen) 中共用，提供统一的运行期性能诊断视图。
25:  * 3. 颜色警示：当平均耗时超过 16.6ms (低于 60FPS 阈值) 或存在丢帧时，以红色醒目提示，便于开发者快速定位掉帧瓶颈。
26:  * 
27:  * 依赖说明:
28:  * - 依赖于 JNI 底层 SDK 的 [VideoFilterManager] 提供的性能观测状态流 [performanceMetrics]。
29:  */
@Composable
fun PerfOverlay(
    filterManager: VideoFilterManager,
    modifier: Modifier = Modifier
) {
    // 订阅来自 C++ 引擎的性能数据流
    val metrics by filterManager.performanceMetrics.collectAsState()
    val avg = metrics?.averageFrameTimeMs ?: 0f
    val p50 = metrics?.p50FrameTimeMs ?: 0f
    val p90 = metrics?.p90FrameTimeMs ?: 0f
    val p99 = metrics?.p99FrameTimeMs ?: 0f
    val dropped = metrics?.droppedFrames ?: 0

    Column(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0x88000000)) // 半透明深色背景，保证在复杂的视频流画面上依然清晰可见
            .padding(horizontal = 10.dp, vertical = 6.dp)
    ) {
        // 平均帧率/耗时：若高于 16ms 则显示警告红，否则显示健康绿
        Text(
            text = "Avg ${"%.1f".format(avg)} ms",
            color = if (avg > 16f) Color(0xFFFF6B6B) else Color(0xFF4ADE80),
            fontSize = 12.sp
        )
        // 分位数指标，帮助分析长尾延迟与卡顿
        Text("P50 ${"%.1f".format(p50)} ms", color = Color.White, fontSize = 11.sp)
        Text("P90 ${"%.1f".format(p90)} ms", color = Color.White, fontSize = 11.sp)
        Text("P99 ${"%.1f".format(p99)} ms", color = Color.White, fontSize = 11.sp)
        
        // 丢帧计数：若存在丢帧显示红色警报
        Text(
            text = "Drop $dropped",
            color = if (dropped > 0) Color(0xFFFF6B6B) else Color.White,
            fontSize = 11.sp
        )
    }
}
