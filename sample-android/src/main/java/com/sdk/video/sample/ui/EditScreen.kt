package com.sdk.video.sample.ui

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
import com.sdk.video.sample.state.AppViewModel
import com.sdk.video.sample.ui.components.ParameterSlider
import com.sdk.video.sample.ui.components.PerfOverlay

/**
 * 编辑页：滤镜横向轨道 + 动态参数 Slider + 转场预览 chip。
 */
@Composable
fun EditScreen(viewModel: AppViewModel) {
    val filterManager by viewModel.filterManager.collectAsState()
    val activeFilters by viewModel.activeFilters.collectAsState()
    val params        by viewModel.filterParams.collectAsState()

    Column(modifier = Modifier.fillMaxSize().background(Color.Black)) {

        // ── Top half: live preview + perf overlay ───────────────
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
                Text("Initializing engine…", color = Color.White)
            }
        }

        // ── Bottom: filter rail + per-filter parameters ─────────
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF111111))
                .padding(vertical = 8.dp)
        ) {
            Text(
                text = "滤镜（点击启用 / 再次点击移除）",
                color = Color(0xFFCCCCCC),
                fontSize = 12.sp,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
            )

            FilterRail(
                active = activeFilters,
                onToggle = { viewModel.toggleFilter(it) }
            )

            // ── Dynamic parameter sliders for each active filter ──
            Column(
                modifier = Modifier
                    .heightIn(max = 240.dp)
                    .verticalScroll(rememberScrollState())
            ) {
                if (activeFilters.isEmpty()) {
                    Text(
                        "未启用任何滤镜",
                        color = Color(0xFF666666),
                        fontSize = 13.sp,
                        modifier = Modifier.padding(16.dp)
                    )
                }

                activeFilters.forEach { filter ->
                    when (filter) {
                        VideoFilterType.BRIGHTNESS -> ParameterSlider(
                            "Brightness", params["brightness"] ?: 0f,
                            { viewModel.setFilterParam("brightness", it) },
                            -0.5f..0.5f
                        )
                        VideoFilterType.GAUSSIAN_BLUR, VideoFilterType.COMPUTE_BLUR ->
                            ParameterSlider(
                                "Blur Size", params["blurSize"] ?: 4f,
                                { viewModel.setFilterParam("blurSize", it) },
                                0f..15f
                            )
                        VideoFilterType.DUAL_KAWASE_BLUR -> {
                            ParameterSlider(
                                "Kawase Iterations",
                                (params["iterations"] ?: 4f),
                                {
                                    // Track in UI as float for slider state, but push int to native
                                    viewModel.setFilterParam("iterations", it)
                                    viewModel.setFilterParam("iterations", it.toInt())
                                },
                                1f..8f,
                                formatValue = { "${it.toInt()}" }
                            )
                            ParameterSlider(
                                "Blur Offset", params["blurOffset"] ?: 1f,
                                { viewModel.setFilterParam("blurOffset", it) },
                                0.5f..3.0f
                            )
                        }
                        VideoFilterType.BLOOM -> {
                            ParameterSlider(
                                "Threshold", params["threshold"] ?: 0.8f,
                                { viewModel.setFilterParam("threshold", it) },
                                0f..1f
                            )
                            ParameterSlider(
                                "Intensity", params["intensity"] ?: 0.6f,
                                { viewModel.setFilterParam("intensity", it) },
                                0f..2f
                            )
                            ParameterSlider(
                                "Knee", params["knee"] ?: 0.1f,
                                { viewModel.setFilterParam("knee", it) },
                                0.01f..0.5f
                            )
                        }
                        VideoFilterType.BILATERAL -> ParameterSlider(
                            "Distance Norm",
                            params["distanceNormalizationFactor"] ?: 8f,
                            { viewModel.setFilterParam("distanceNormalizationFactor", it) },
                            1f..20f
                        )
                        VideoFilterType.LOOKUP, VideoFilterType.CINEMATIC_LOOKUP,
                        VideoFilterType.LUT3D -> ParameterSlider(
                            "Intensity", params["intensity"] ?: 1f,
                            { viewModel.setFilterParam("intensity", it) },
                            0f..1f
                        )
                        VideoFilterType.NIGHT_VISION -> Text(
                            "夜视滤镜（无可调参数）",
                            color = Color(0xFF888888), fontSize = 12.sp,
                            modifier = Modifier.padding(16.dp)
                        )
                    }
                }
            }

            // ── Transition preview chips (informational) ───────
            Text(
                "转场预览（10 个内置 shader）",
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
                    "crossfade", "wipe_left", "wipe_right", "wipe_up", "wipe_down",
                    "slide_left", "slide_right", "zoom_in", "fade_black", "flash"
                ).forEach { name ->
                    AssistChip(
                        onClick = { /* preview hook stub */ },
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
    VideoFilterType.LOOKUP            -> "LUT 滤镜"
    VideoFilterType.BILATERAL         -> "双边磨皮"
    VideoFilterType.CINEMATIC_LOOKUP  -> "电影滤镜"
    VideoFilterType.COMPUTE_BLUR      -> "Compute 模糊"
    VideoFilterType.NIGHT_VISION      -> "夜视"
    VideoFilterType.LUT3D             -> "3D LUT"
    VideoFilterType.DUAL_KAWASE_BLUR  -> "Dual Kawase"
    VideoFilterType.BLOOM             -> "Bloom"
}
