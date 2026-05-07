package com.sdk.video.sample.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sdk.video.sample.state.ColorParams
import com.sdk.video.sample.state.TimelineTool
import com.sdk.video.sample.state.TimelineViewModel
import com.sdk.video.timeline.TimelineManager

private val Orange = Color(0xFFFF6B00)
private val PanelBg = Color(0xFF141414)
private val ToolBarBg = Color(0xFF0F0F0F)

data class ToolItem(val tool: TimelineTool, val icon: ImageVector, val label: String)

val TOOL_ITEMS = listOf(
    ToolItem(TimelineTool.COLOR,      Icons.Filled.Tune,         "调色"),
    ToolItem(TimelineTool.TEXT,       Icons.Filled.Title,        "文字"),
    ToolItem(TimelineTool.AUDIO,      Icons.Filled.MusicNote,    "音频"),
    ToolItem(TimelineTool.EFFECTS,    Icons.Filled.AutoFixHigh,  "特效"),
    ToolItem(TimelineTool.TRANSITION, Icons.Filled.Layers,       "转场"),
    ToolItem(TimelineTool.SPEED,      Icons.Filled.Speed,        "变速"),
)

@Composable
fun ToolBar(
    activeTool: TimelineTool?,
    onToolClick: (TimelineTool) -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(ToolBarBg)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 12.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        TOOL_ITEMS.forEach { item ->
            val isActive = activeTool == item.tool
            Column(
                modifier = Modifier
                    .clip(RoundedCornerShape(10.dp))
                    .background(if (isActive) Orange.copy(alpha = 0.15f) else Color.Transparent)
                    .clickable { onToolClick(item.tool) }
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Icon(
                    item.icon,
                    contentDescription = item.label,
                    tint = if (isActive) Orange else Color(0xFFCCCCCC),
                    modifier = Modifier.size(22.dp)
                )
                Text(
                    item.label,
                    color = if (isActive) Orange else Color(0xFFCCCCCC),
                    fontSize = 11.sp,
                    fontWeight = if (isActive) FontWeight.Bold else FontWeight.Normal
                )
            }
        }
    }
}

@Composable
fun ToolPanelContainer(
    activeTool: TimelineTool?,
    timelineVm: TimelineViewModel,
    onNavigateToText: () -> Unit
) {
    val colorParams by timelineVm.colorParams.collectAsState()
    val speedFactor  by timelineVm.speedFactor.collectAsState()

    AnimatedVisibility(
        visible = activeTool != null,
        enter = expandVertically(expandFrom = Alignment.Top),
        exit  = shrinkVertically(shrinkTowards = Alignment.Top)
    ) {
        when (activeTool) {
            TimelineTool.COLOR      -> ColorGradingPanel(colorParams) { key, v -> timelineVm.setColorParam(key, v) }
            TimelineTool.TEXT       -> TextToolPanel(onNavigateToText)
            TimelineTool.AUDIO      -> AudioPanel(timelineVm)
            TimelineTool.EFFECTS    -> EffectsToolPanel()
            TimelineTool.TRANSITION -> TransitionPanel(timelineVm)
            TimelineTool.SPEED      -> SpeedPanel(speedFactor) { timelineVm.setSpeed(it) }
            null -> {}
        }
    }
}

@Composable
fun ColorGradingPanel(
    params: ColorParams,
    onParamChange: (String, Float) -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        PanelTitle("调色")
        ColorSlider("亮度", params.brightness, -1f..1f)  { onParamChange("brightness", it) }
        ColorSlider("对比度", params.contrast,   -1f..1f)  { onParamChange("contrast", it) }
        ColorSlider("饱和度", params.saturation, -1f..1f)  { onParamChange("saturation", it) }
        ColorSlider("色温",  params.temperature, -1f..1f)  { onParamChange("temperature", it) }
        ColorSlider("锐化",  params.sharpen,      0f..1f)  { onParamChange("sharpen", it) }
    }
}

@Composable
private fun ColorSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onValueChange: (Float) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = Color(0xFFCCCCCC), fontSize = 13.sp, modifier = Modifier.width(52.dp))
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = range,
            modifier = Modifier.weight(1f),
            colors = SliderDefaults.colors(thumbColor = Orange, activeTrackColor = Orange)
        )
        Text(
            "%.2f".format(value),
            color = Color(0xFF888888),
            fontSize = 11.sp,
            modifier = Modifier.width(36.dp),
            textAlign = androidx.compose.ui.text.style.TextAlign.End
        )
    }
}

@Composable
fun AudioPanel(timelineVm: TimelineViewModel) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        PanelTitle("音频")

        var fadeIn  by remember { mutableStateOf(false) }
        var fadeOut by remember { mutableStateOf(false) }
        var noiseRed by remember { mutableStateOf(0f) }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            AudioSwitch(label = "淡入", checked = fadeIn) {
                fadeIn = it
                if (it) timelineVm.setAudioFadeIn(500_000L)
            }
            AudioSwitch(label = "淡出", checked = fadeOut) {
                fadeOut = it
                if (it) timelineVm.setAudioFadeOut(500_000L)
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("降噪", color = Color(0xFFCCCCCC), fontSize = 13.sp, modifier = Modifier.width(52.dp))
            Slider(
                value = noiseRed,
                onValueChange = {
                    noiseRed = it
                    timelineVm.setNoiseReduction(it)
                },
                valueRange = 0f..1f,
                modifier = Modifier.weight(1f),
                colors = SliderDefaults.colors(thumbColor = Orange, activeTrackColor = Orange)
            )
            Text("%.0f%%".format(noiseRed * 100), color = Color(0xFF888888), fontSize = 11.sp, modifier = Modifier.width(36.dp), textAlign = androidx.compose.ui.text.style.TextAlign.End)
        }
    }
}

@Composable
private fun AudioSwitch(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (checked) Orange.copy(alpha = 0.15f) else Color(0xFF1E1E1E))
            .border(1.dp, if (checked) Orange else Color(0xFF333333), RoundedCornerShape(8.dp))
            .clickable { onCheckedChange(!checked) }
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Text(label, color = if (checked) Orange else Color(0xFFCCCCCC), fontSize = 13.sp)
    }
}

@Composable
fun SpeedPanel(current: Float, onSpeedChange: (Float) -> Unit) {
    val speeds = listOf(0.25f to "0.25×", 0.5f to "0.5×", 1f to "1×", 1.5f to "1.5×", 2f to "2×", 4f to "4×")
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        PanelTitle("变速")
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            speeds.forEach { (speed, label) ->
                val isActive = (current == speed)
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .height(38.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (isActive) Orange else Color(0xFF1E1E1E))
                        .border(1.dp, if (isActive) Orange else Color(0xFF333333), RoundedCornerShape(8.dp))
                        .clickable { onSpeedChange(speed) },
                    contentAlignment = Alignment.Center
                ) {
                    Text(label, color = Color.White, fontSize = 12.sp, fontWeight = if (isActive) FontWeight.Bold else FontWeight.Normal)
                }
            }
        }
        Text(
            "慢动作 / 快动作",
            color = Color(0xFF666666),
            fontSize = 11.sp,
            modifier = Modifier.align(Alignment.CenterHorizontally)
        )
    }
}

@Composable
fun TransitionPanel(timelineVm: TimelineViewModel) {
    val transitions = listOf(
        "无" to TimelineManager.TransitionType.NONE,
        "淡入淡出" to TimelineManager.TransitionType.CROSSFADE,
        "闪白" to TimelineManager.TransitionType.FLASH,
        "擦除" to TimelineManager.TransitionType.WIPE_LEFT,
        "滑动" to TimelineManager.TransitionType.SLIDE_LEFT,
        "缩放" to TimelineManager.TransitionType.ZOOM_IN,
    )
    var selected by remember { mutableStateOf(TimelineManager.TransitionType.NONE) }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        PanelTitle("转场")
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            transitions.forEach { (label, type) ->
                TransitionChip(
                    label = label,
                    isSelected = selected == type
                ) {
                    selected = type
                    timelineVm.setTransition(type, 500_000L)
                }
            }
        }
    }
}

@Composable
private fun TransitionChip(label: String, isSelected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (isSelected) Orange else Color(0xFF1E1E1E))
            .border(1.dp, if (isSelected) Orange else Color(0xFF333333), RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = Color.White, fontSize = 13.sp)
    }
}

@Composable
fun EffectsToolPanel() {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        PanelTitle("特效")
        Text("前往「拍摄」Tab 可实时预览特效，此处展示已加载包:", color = Color(0xFF888888), fontSize = 12.sp)
        Row(
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            listOf("暖光滤镜", "可爱妆容", "粉色发色").forEach { name ->
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF1E1E1E))
                        .border(1.dp, Color(0xFF333333), RoundedCornerShape(8.dp))
                        .padding(horizontal = 12.dp, vertical = 8.dp)
                ) {
                    Text(name, color = Color(0xFFCCCCCC), fontSize = 13.sp)
                }
            }
        }
    }
}

@Composable
fun TextToolPanel(onNavigateToText: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        PanelTitle("文字")
        Button(
            onClick = onNavigateToText,
            modifier = Modifier.fillMaxWidth(),
            colors = ButtonDefaults.buttonColors(containerColor = Orange),
            shape = RoundedCornerShape(10.dp)
        ) {
            Icon(Icons.Filled.Add, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("添加文字 / 字幕", fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun PanelTitle(title: String) {
    Text(title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
}
