@file:OptIn(com.sdk.video.InternalApi::class)
package com.sdk.video.sample.ui

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
import com.sdk.video.sample.state.AppViewModel
import kotlinx.coroutines.delay

/**
 * 诊断页：设备硬件信息 + GL 能力位 + 软解可用性 + 实时性能折线。
 */
@Composable
fun DiagnosticsScreen(viewModel: AppViewModel) {
    val deviceCaps    by viewModel.deviceCaps.collectAsState()
    val softDecoder   by viewModel.softwareDecoderAvailable.collectAsState()
    val filterManager by viewModel.filterManager.collectAsState()

    // Pull device caps once filterManager is ready
    LaunchedEffect(filterManager) {
        filterManager?.let { viewModel.refreshDeviceCaps() }
    }

    // Rolling P90 history (60 samples)
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
        Text("设备诊断", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(12.dp))

        // ── Device info ─────────────────────────────────────────
        SectionCard("设备信息") {
            DiagRow("制造商",   Build.MANUFACTURER)
            DiagRow("型号",     Build.MODEL)
            DiagRow("Android",  "${Build.VERSION.RELEASE}  (SDK ${Build.VERSION.SDK_INT})")
            DiagRow("ABI",      Build.SUPPORTED_ABIS.firstOrNull() ?: "unknown")
        }

        Spacer(Modifier.height(12.dp))

        // ── GL capabilities ─────────────────────────────────────
        SectionCard("OpenGL ES 能力") {
            if (deviceCaps == null) {
                Text("引擎未就绪", color = Color(0xFF888888), fontSize = 13.sp)
            } else {
                val c = deviceCaps!!
                DiagRow("GLES 版本",     glesLabel(c.glesVersion))
                DiagRow("Compute Shader",   boolLabel(c.computeShader))
                DiagRow("FP16 渲染目标",   boolLabel(c.fp16))
                DiagRow("ASTC 压缩纹理",   boolLabel(c.astc))
                DiagRow("Vulkan 后端",     boolLabel(c.vulkan))
                DiagRow("Geometry Shader", boolLabel(c.geometryShader))
                DiagRow("最大 MSAA 倍率",  "${c.maxMSAA}×")
            }
        }

        Spacer(Modifier.height(12.dp))

        // ── Decoding capabilities ───────────────────────────────
        SectionCard("解码与导出") {
            DiagRow("FFmpeg 软解可用", boolLabel(softDecoder))
            DiagRow("MediaCodec 硬解",  "Android 标配（始终可用）")
        }

        Spacer(Modifier.height(12.dp))

        // ── Real-time perf chart ────────────────────────────────
        SectionCard("实时 P90 帧耗 (ms)") {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(120.dp)
                    .clip(RoundedCornerShape(6.dp))
                    .background(Color(0xFF0A0A0A))
                    .padding(8.dp)
            ) {
                if (p90History.isEmpty()) {
                    Text("采样中…", color = Color(0xFF555555),
                         modifier = Modifier.align(Alignment.Center))
                } else {
                    P90LineChart(p90History.toList())
                }
            }
            Text(
                "采样间隔 500 ms · 窗口 60 点 · 16.7ms 红线 = 60fps 阈值",
                color = Color(0xFF666666), fontSize = 10.sp,
                modifier = Modifier.padding(top = 4.dp)
            )
        }

        Spacer(Modifier.height(20.dp))

        Button(
            onClick = { viewModel.refreshDeviceCaps() },
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF333333)),
            modifier = Modifier.fillMaxWidth()
        ) { Text("重新探测设备能力", color = Color.White) }
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

@Composable
private fun P90LineChart(history: List<Float>) {
    val maxVal = (history.maxOrNull() ?: 30f).coerceAtLeast(20f)
    Canvas(modifier = Modifier.fillMaxSize()) {
        val w = size.width
        val h = size.height

        // 16.7ms threshold line (60 fps)
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
        drawPath(path, color = Color(0xFF4ADE80),
                 style = androidx.compose.ui.graphics.drawscope.Stroke(width = 2f))
    }
}

private fun boolLabel(v: Boolean) = if (v) "✓ 支持" else "✗ 不支持"
private fun glesLabel(version: Int) = "GLES " + (version / 10) + "." + (version % 10)
