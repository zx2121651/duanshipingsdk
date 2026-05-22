@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.sample.feature.diagnostics

import android.os.Build
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sdk.video.sample.core.model.AppViewModel
import kotlinx.coroutines.delay

/**
 * 引擎运行期与设备硬件诊断页面 - Feature Diagnostics Component
 * 
 * 核心逻辑与功能:
 * 1. 硬件/系统信息快照：读取 Build 信息展现 Android 系统的底层 ABI、制造厂商与 SDK 版本。
 * 2. 引擎能力诊断 (对标剪映/抖音渲染适配系统)：
 *    - 核心参数：GLES 版本、Compute Shader、FP16 高动态渲染目标、ASTC 纹理压缩、Vulkan 后端以及多重采样抗锯齿 (MSAA)。
 * 3. 实时性能折线图渲染：
 *    - 定时轮询：每 500ms 抓取一次底层渲染器当前帧耗时的 P90 指标。
 *    - 滑动窗口：维护一个容量为 60 的环形缓冲区（展示最近 30 秒性能波动）。
 *    - 自定义绘制：利用 Compose 的 [Canvas] 绘制带 16.7ms 警戒红线的性能折线图。
 */
@Composable
fun DiagnosticsScreen(viewModel: AppViewModel) {
    val deviceCaps    by viewModel.deviceCaps.collectAsState()
    val softDecoder   by viewModel.softwareDecoderAvailable.collectAsState()
    val filterManager by viewModel.filterManager.collectAsState()

    // 引擎就绪后刷新探测 OpenGL/Vulkan 硬件兼容性能力位
    LaunchedEffect(filterManager) {
        filterManager?.let { viewModel.refreshDeviceCaps() }
    }

    // 性能波动历史环形缓冲区 (限制 60 个点，显示 30 秒内的趋势)
    val p90History = remember { mutableStateListOf<Float>() }
    LaunchedEffect(filterManager) {
        while (true) {
            delay(500)
            val p90 = filterManager?.performanceMetrics?.value?.p90FrameTimeMs ?: 0f
            if (p90 > 0f) {
                if (p90History.size >= 60) p90History.removeAt(0)
                p90History.add(p90)
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
            .verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
        Text("设备诊断与监控", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(12.dp))

        // 1. 系统与芯片环境信息卡片
        SectionCard("设备环境") {
            DiagRow("芯片制造商",   Build.MANUFACTURER)
            DiagRow("设备型号",     Build.MODEL)
            DiagRow("Android 版本",  "${Build.VERSION.RELEASE}  (API ${Build.VERSION.SDK_INT})")
            DiagRow("支持架构 (ABI)", Build.SUPPORTED_ABIS.firstOrNull() ?: "unknown")
        }

        Spacer(Modifier.height(12.dp))

        // 2. 视频渲染引擎底层图形 API 能力位卡片
        SectionCard("底层图形 API 兼容状态") {
            if (deviceCaps == null) {
                Text("等待视频 SDK 引擎初始化并绑定渲染 Surface...", color = Color(0xFF888888), fontSize = 13.sp)
            } else {
                val c = deviceCaps!!
                DiagRow("GLES 版本",     glesLabel(c.glesVersion))
                DiagRow("Compute 着色器",   boolLabel(c.computeShader))
                DiagRow("FP16 高清纹理格式",   boolLabel(c.fp16))
                DiagRow("ASTC 压缩纹理格式",   boolLabel(c.astc))
                DiagRow("Vulkan 图形后端",     boolLabel(c.vulkan))
                DiagRow("几何着色器 (Geometry)", boolLabel(c.geometryShader))
                DiagRow("最大 MSAA 抗锯齿倍数",  "${c.maxMSAA}×")
            }
        }

        Spacer(Modifier.height(12.dp))

        // 3. 编解码组件兼容卡片
        SectionCard("底层解码与导出架构") {
            DiagRow("FFmpeg 软解组件 (软件兜底)", boolLabel(softDecoder))
            DiagRow("Android MediaCodec (硬解加速)", "已自动匹配高性能硬件队列 (始终开启)")
        }

        Spacer(Modifier.height(12.dp))

        // 4. 实时性能趋势折线图
        SectionCard("渲染引擎实时 P90 帧耗时趋势 (ms)") {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(120.dp)
                    .clip(RoundedCornerShape(6.dp))
                    .background(Color(0xFF0A0A0A))
                    .padding(8.dp)
            ) {
                if (p90History.isEmpty()) {
                    Text("等待引擎渲染帧以激活采样...", color = Color(0xFF555555),
                         modifier = Modifier.align(Alignment.Center))
                } else {
                    P90LineChart(p90History.toList())
                }
            }
            Text(
                "指标说明：采样间隔 500 ms · 滑动窗口 60 个采样点 · 16.7ms 红线为 60FPS 流畅阈值",
                color = Color(0xFF666666), fontSize = 10.sp,
                modifier = Modifier.padding(top = 4.dp)
            )
        }

        Spacer(Modifier.height(20.dp))

        Button(
            onClick = { viewModel.refreshDeviceCaps() },
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF333333)),
            modifier = Modifier.fillMaxWidth()
        ) { Text("重新刷新检测", color = Color.White) }
    }
}

@Composable
private fun SectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(
        colors = CardDefaults.cardColors(containerColor = Color(0xFF141414)),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(title, color = Color.White, fontSize = 14.sp,
                 fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(8.dp))
            content()
        }
    }
}

@Composable
private fun DiagRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = Color(0xFFAAAAAA), fontSize = 13.sp,
             modifier = Modifier.weight(1f))
        Text(value, color = Color.White, fontSize = 13.sp,
             fontFamily = FontFamily.Monospace)
    }
}

/**
 * 实时绘制 P90 帧率渲染折线图
 */
@Composable
private fun P90LineChart(history: List<Float>) {
    val maxVal = (history.maxOrNull() ?: 30f).coerceAtLeast(20f)
    Canvas(modifier = Modifier.fillMaxSize()) {
        val w = size.width
        val h = size.height

        // 绘制 16.7ms 的 60 FPS 流畅判定红线
        val threshY = h * (1f - 16.7f / maxVal).coerceIn(0f, 1f)
        drawLine(
            color = Color(0x66FF6B6B),
            start = Offset(0f, threshY),
            end   = Offset(w, threshY),
            strokeWidth = 1.5f
        )

        if (history.size < 2) return@Canvas

        val dx = w / (history.size - 1).toFloat()
        val path = Path()
        history.forEachIndexed { i, v ->
            val x = i * dx
            val y = h * (1f - (v / maxVal).coerceIn(0f, 1f))
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        drawPath(
            path = path,
            color = Color(0xFF4ADE80), // 健康绿色折线
            style = androidx.compose.ui.graphics.drawscope.Stroke(width = 2f)
        )
    }
}

private fun boolLabel(v: Boolean) = if (v) "✓ 支持" else "✗ 不支持"
private fun glesLabel(version: Int) = "GLES " + (version / 10) + "." + (version % 10)
