package com.sdk.video.sample.feature.effects

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sdk.video.FilterCameraPreview
import com.sdk.video.VideoFilterType
import com.sdk.video.sample.core.model.AppViewModel
import com.sdk.video.sample.core.ui.components.ParameterSlider
import com.sdk.video.sample.core.ui.components.PerfOverlay

/**
 * 视频效果/滤镜高级编辑页 - Feature Effects/Edit Component
 * 
 * 核心逻辑与功能:
 * 1. 滤镜渲染器组合：支持动态开启/叠加多个 OpenGL/Vulkan 滤镜着色器。
 * 2. 实时参数滑块：针对不同的 [VideoFilterType]，渲染特定范围与步长的滑块调节控件 (如 Brightness、Gaussian Blur、Kawase Blur 等)。
 * 3. 转场模拟预览：展示十余种内置转场着色器切换效果芯片。
 * 
 * 设计规范:
 * - 采用深黑色调设计系统，突出预览轨道与渲染色彩。
 * - 性能反馈：右上角悬浮显示 `PerfOverlay` 指标。
 */
@Composable
fun EditScreen(viewModel: AppViewModel) {
    val filterManager by viewModel.filterManager.collectAsState()
    val activeFilters by viewModel.activeFilters.collectAsState()
    val params        by viewModel.filterParams.collectAsState()

    Column(modifier = Modifier.fillMaxSize().background(Color.Black)) {

        // ── 上半部分: 视频流实时预览与性能浮层 ───────────────
        Box(modifier = Modifier.fillMaxWidth().weight(1f)) {
            filterManager?.let { fm ->
                FilterCameraPreview(filterManager = fm, modifier = Modifier.fillMaxSize())
                PerfOverlay(
                    filterManager = fm,
                    modifier = Modifier
                        .align(Alignment.TopStart)
                        .padding(start = 12.dp, top = 12.dp)
                )
            } ?: Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("正在初始化滤镜引擎...", color = Color.White)
            }
        }

        // ── 下半部分: 滤镜选项轨道 & 细节参数滑块 ─────────
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF111111))
                .padding(vertical = 8.dp)
        ) {
            Text(
                text = "滤镜列表 (点击切换启用状态)",
                color = Color(0xFFCCCCCC),
                fontSize = 12.sp,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
            )

            // 滤镜横向横扫轨道
            FilterRail(
                active = activeFilters,
                onToggle = { viewModel.toggleFilter(it) }
            )

            // ── 根据启用的滤镜，动态映射渲染其特有的调节滑块 ──
            Column(
                modifier = Modifier
                    .heightIn(max = 240.dp)
                    .verticalScroll(rememberScrollState())
            ) {
                if (activeFilters.isEmpty()) {
                    Text(
                        "当前未启用滤镜，点击上方选项开启体验",
                        color = Color(0xFF666666),
                        fontSize = 13.sp,
                        modifier = Modifier.padding(16.dp)
                    )
                }

                activeFilters.forEach { filter ->
                    when (filter) {
                        VideoFilterType.BRIGHTNESS -> ParameterSlider(
                            "亮度 (Brightness)", params["brightness"] ?: 0f,
                            { viewModel.setFilterParam("brightness", it) },
                            -0.5f..0.5f
                        )
                        VideoFilterType.GAUSSIAN_BLUR, VideoFilterType.COMPUTE_BLUR ->
                            ParameterSlider(
                                "模糊半径 (Blur Size)", params["blurSize"] ?: 4f,
                                { viewModel.setFilterParam("blurSize", it) },
                                0f..15f
                            )
                        VideoFilterType.DUAL_KAWASE_BLUR -> {
                            ParameterSlider(
                                "Kawase 迭代次数",
                                (params["iterations"] ?: 4f),
                                {
                                    viewModel.setFilterParam("iterations", it)
                                    viewModel.setFilterParam("iterations", it.toInt())
                                },
                                1f..8f,
                                formatValue = { "${it.toInt()}" }
                            )
                            ParameterSlider(
                                "Kawase 模糊偏移", params["blurOffset"] ?: 1f,
                                { viewModel.setFilterParam("blurOffset", it) },
                                0.5f..3.0f
                            )
                        }
                        VideoFilterType.BLOOM -> {
                            ParameterSlider(
                                "阈值 (Threshold)", params["threshold"] ?: 0.8f,
                                { viewModel.setFilterParam("threshold", it) },
                                0f..1f
                            )
                            ParameterSlider(
                                "强度 (Intensity)", params["intensity"] ?: 0.6f,
                                { viewModel.setFilterParam("intensity", it) },
                                0f..2f
                            )
                            ParameterSlider(
                                "缓弯 (Knee)", params["knee"] ?: 0.1f,
                                { viewModel.setFilterParam("knee", it) },
                                0.01f..0.5f
                            )
                        }
                        VideoFilterType.BILATERAL -> ParameterSlider(
                            "磨皮保边系数 (Bilateral)",
                            params["distanceNormalizationFactor"] ?: 8f,
                            { viewModel.setFilterParam("distanceNormalizationFactor", it) },
                            1f..20f
                        )
                        VideoFilterType.LOOKUP, VideoFilterType.CINEMATIC_LOOKUP,
                        VideoFilterType.LUT3D -> ParameterSlider(
                            "色彩映射强度", params["intensity"] ?: 1f,
                            { viewModel.setFilterParam("intensity", it) },
                            0f..1f
                        )
                        VideoFilterType.NIGHT_VISION -> Text(
                            "夜视滤镜 (全自动调节，无可调参数)",
                            color = Color(0xFF888888), fontSize = 12.sp,
                            modifier = Modifier.padding(16.dp)
                        )
                        VideoFilterType.PROP_OVERLAY -> Text(
                            "道具贴纸覆盖 (请在特效中心选择指定素材包)",
                            color = Color(0xFF888888), fontSize = 12.sp,
                            modifier = Modifier.padding(16.dp)
                        )
                    }
                }
            }

            // ── 转场预设面板 (供预览对标剪映 10 个内置着色器) ──
            Text(
                "转场效果 (10 款内置 Shader 预设)",
                color = Color(0xFFCCCCCC), fontSize = 12.sp,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState())
                    .padding(horizontal = 12.dp, vertical = 4.dp)
                ) {
                listOf(
                    "叠化 (crossfade)", "向左擦除 (wipe_left)", "向右擦除 (wipe_right)", 
                    "向上擦除 (wipe_up)", "向下擦除 (wipe_down)", "向左滑入 (slide_left)", 
                    "向右滑入 (slide_right)", "缩放 (zoom_in)", "黑场过渡 (fade_black)", "闪白 (flash)"
                ).forEach { name ->
                    AssistChip(
                        onClick = { /* 触发转场模拟预览 */ },
                        label = { Text(name, fontSize = 11.sp) },
                        modifier = Modifier.padding(end = 6.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun FilterRail(
    active: List<VideoFilterType>,
    onToggle: (VideoFilterType) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 8.dp, vertical = 6.dp)
    ) {
        VideoFilterType.values().forEach { type ->
            val on = active.contains(type)
            FilterChipButton(
                label = labelFor(type),
                selected = on,
                onClick = { onToggle(type) }
            )
        }
    }
}

@Composable
private fun FilterChipButton(label: String, selected: Boolean, onClick: () -> Unit) {
    AssistChip(
        onClick = onClick,
        label = { Text(label, fontSize = 12.sp) },
        colors = AssistChipDefaults.assistChipColors(
            containerColor  = if (selected) Color(0xFF4ADE80) else Color(0xFF2A2A2A),
            labelColor      = if (selected) Color.Black     else Color.White
        ),
        modifier = Modifier.padding(horizontal = 4.dp)
    )
}

private fun labelFor(t: VideoFilterType): String = when (t) {
    VideoFilterType.BRIGHTNESS        -> "亮度"
    VideoFilterType.GAUSSIAN_BLUR     -> "高斯模糊"
    VideoFilterType.LOOKUP            -> "LUT 色彩"
    VideoFilterType.BILATERAL         -> "双边磨皮"
    VideoFilterType.CINEMATIC_LOOKUP  -> "胶片电影"
    VideoFilterType.COMPUTE_BLUR      -> "Compute 模糊"
    VideoFilterType.NIGHT_VISION      -> "微光夜视"
    VideoFilterType.LUT3D             -> "3D LUT"
    VideoFilterType.DUAL_KAWASE_BLUR  -> "Dual Kawase"
    VideoFilterType.BLOOM             -> "辉光 (Bloom)"
    VideoFilterType.PROP_OVERLAY      -> "道具叠加"
}
