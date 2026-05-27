package com.sdk.video.sample.feature.editor

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Title
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sdk.video.sample.core.model.ColorParams
import com.sdk.video.sample.core.model.TimelineTool
import com.sdk.video.sample.core.model.TimelineViewModel
import com.sdk.video.timeline.TimelineManager

private val Accent = Color(0xFF00D7FF)
private val PanelBg = Color(0xFF141414)
private val ToolBarBg = Color(0xFF0F0F0F)

data class ToolItem(val tool: TimelineTool, val icon: ImageVector, val label: String)

val TOOL_ITEMS = listOf(
    ToolItem(TimelineTool.COLOR, Icons.Filled.Tune, "调色"),
    ToolItem(TimelineTool.TEXT, Icons.Filled.Title, "文字"),
    ToolItem(TimelineTool.AUDIO, Icons.Filled.MusicNote, "音频"),
    ToolItem(TimelineTool.EFFECTS, Icons.Filled.AutoFixHigh, "特效"),
    ToolItem(TimelineTool.TRANSITION, Icons.Filled.Layers, "转场"),
    ToolItem(TimelineTool.SPEED, Icons.Filled.Speed, "变速"),
)

/**
 * 剪辑工具底栏面板 - Feature Editor/Toolbar Component
 * 
 * 展示“调色”、“文字”、“音频”、“特效”、“转场”、“变速”快捷按钮，承载选中工具状态的高亮和点击分发。
 */
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
                    .background(if (isActive) Accent.copy(alpha = 0.16f) else Color.Transparent)
                    .clickable { onToolClick(item.tool) }
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Icon(
                    item.icon,
                    contentDescription = item.label,
                    tint = if (isActive) Accent else Color(0xFFCCCCCC),
                    modifier = Modifier.size(22.dp)
                )
                Text(
                    item.label,
                    color = if (isActive) Accent else Color(0xFFCCCCCC),
                    fontSize = 11.sp,
                    fontWeight = if (isActive) FontWeight.Bold else FontWeight.Normal
                )
            }
        }
    }
}

/**
 * 编辑属性面板切换容器 - Feature Editor/Panel Container Component
 * 
 * 根据当前选中的 [activeTool]，动态挂载相应的二级控制面板，并在无选中时折叠面板区域。
 */
@Composable
fun ToolPanelContainer(
    activeTool: TimelineTool?,
    timelineVm: TimelineViewModel,
    onNavigateToText: () -> Unit
) {
    val colorParams by timelineVm.colorParams.collectAsState()
    val speedFactor by timelineVm.speedFactor.collectAsState()

    AnimatedVisibility(
        visible = activeTool != null,
        enter = expandVertically(expandFrom = Alignment.Top),
        exit = shrinkVertically(shrinkTowards = Alignment.Top)
    ) {
        when (activeTool) {
            TimelineTool.COLOR -> ColorGradingPanel(colorParams) { key, value -> timelineVm.setColorParam(key, value) }
            TimelineTool.TEXT -> TextToolPanel(onNavigateToText)
            TimelineTool.AUDIO -> AudioPanel(timelineVm)
            TimelineTool.EFFECTS -> EffectsToolPanel()
            TimelineTool.TRANSITION -> TransitionPanel(timelineVm)
            TimelineTool.SPEED -> SpeedPanel(speedFactor) { timelineVm.setSpeed(it) }
            null -> Unit
        }
    }
}

/**
 * 调色控制面板 (Color Grading Panel)
 * 
 * 功能:
 * 1. 经典三向调色盘 (Lift / Gamma / Gain)：通过拖拽模拟控制视频画面的“阴影”、“中间调”、“高光”。
 * 2. 基础调色滑块：亮度 (brightness)、对比度 (contrast)、饱和度 (saturation)、色温 (temperature) 与锐化 (sharpen)。
 */
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
        PanelTitle("专业调色")

        // 三向调色滚轮并排布局
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 10.dp),
            horizontalArrangement = Arrangement.SpaceEvenly
        ) {
            ColorWheel("阴影 Offset")
            ColorWheel("中间调 Gamma")
            ColorWheel("高光 Gain")
        }

        Spacer(Modifier.height(4.dp))

        ColorSlider("亮度", params.brightness, -1f..1f) { onParamChange("brightness", it) }
        ColorSlider("对比", params.contrast, -1f..1f) { onParamChange("contrast", it) }
        ColorSlider("饱和", params.saturation, -1f..1f) { onParamChange("saturation", it) }
        ColorSlider("色温", params.temperature, -1f..1f) { onParamChange("temperature", it) }
        ColorSlider("锐化", params.sharpen, 0f..1f) { onParamChange("sharpen", it) }
    }
}

@Composable
private fun ColorWheel(
    label: String,
    modifier: Modifier = Modifier
) {
    var offsetX by remember { mutableStateOf(0f) }
    var offsetY by remember { mutableStateOf(0f) }

    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = modifier
    ) {
        Text(
            text = label,
            color = Color(0xFF888892),
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(Modifier.height(8.dp))

        // 模拟色彩光谱滚轮，支持手势拖动监听
        Box(
            modifier = Modifier
                .size(72.dp)
                .clip(CircleShape)
                .background(
                    Brush.sweepGradient(
                        colors = listOf(
                            Color(0xFFFF3B30), // 红色
                            Color(0xFFFFCC00), // 黄色
                            Color(0xFF34C759), // 绿色
                            Color(0xFF00C7BE), // 青色
                            Color(0xFF007AFF), // 蓝色
                            Color(0xFFAF52DE), // 紫色
                            Color(0xFFFF3B30)
                        )
                    )
                )
                .border(1.2.dp, Color(0xFF222226), CircleShape)
                .pointerInput(Unit) {
                    detectDragGestures { change, dragAmount ->
                        change.consume()
                        val newX = (offsetX + dragAmount.x).coerceIn(-60f, 60f)
                        val newY = (offsetY + dragAmount.y).coerceIn(-60f, 60f)
                        val dist = Math.sqrt((newX * newX + newY * newY).toDouble())
                        val maxDist = 50f
                        if (dist <= maxDist) {
                            offsetX = newX
                            offsetY = newY
                        } else {
                            val angle = Math.atan2(newY.toDouble(), newX.toDouble())
                            offsetX = (maxDist * Math.cos(angle)).toFloat()
                            offsetY = (maxDist * Math.sin(angle)).toFloat()
                        }
                    }
                },
            contentAlignment = Alignment.Center
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(
                        Brush.radialGradient(
                            colors = listOf(Color.Black.copy(alpha = 0.25f), Color.Transparent),
                            radius = 120f
                        )
                    )
            )

            // 中央定位指示滑块
            Box(
                modifier = Modifier
                    .offset(x = (offsetX / 2.5f).dp, y = (offsetY / 2.5f).dp)
                    .size(10.dp)
                    .clip(CircleShape)
                    .background(Color.White)
                    .border(1.5.dp, Color.Black, CircleShape)
            )
        }
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
            colors = SliderDefaults.colors(
                thumbColor = Color.White,
                activeTrackColor = Accent,
                inactiveTrackColor = Color(0xFF222226)
            )
        )
        Text(
            "%.2f".format(value),
            color = Color(0xFF888892),
            fontSize = 11.sp,
            modifier = Modifier.width(38.dp),
            textAlign = TextAlign.End
        )
    }
}

/**
 * 音频处理属性面板
 */
@Composable
fun AudioPanel(timelineVm: TimelineViewModel) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        PanelTitle("音频处理")

        var fadeIn by remember { mutableStateOf(false) }
        var fadeOut by remember { mutableStateOf(false) }
        var noiseReduction by remember { mutableStateOf(0f) }

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            ToggleChip(label = "淡入", checked = fadeIn) {
                fadeIn = it
                if (it) timelineVm.setAudioFadeIn(500_000L)
            }
            ToggleChip(label = "淡出", checked = fadeOut) {
                fadeOut = it
                if (it) timelineVm.setAudioFadeOut(500_000L)
            }
        }

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("降噪", color = Color(0xFFCCCCCC), fontSize = 13.sp, modifier = Modifier.width(52.dp))
            Slider(
                value = noiseReduction,
                onValueChange = {
                    noiseReduction = it
                    timelineVm.setNoiseReduction(it)
                },
                valueRange = 0f..1f,
                modifier = Modifier.weight(1f),
                colors = SliderDefaults.colors(thumbColor = Accent, activeTrackColor = Accent)
            )
            Text("%.0f%%".format(noiseReduction * 100), color = Color(0xFF888888), fontSize = 11.sp, modifier = Modifier.width(38.dp), textAlign = TextAlign.End)
        }
    }
}

@Composable
private fun ToggleChip(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (checked) Accent.copy(alpha = 0.16f) else Color(0xFF1E1E1E))
            .border(1.dp, if (checked) Accent else Color(0xFF333333), RoundedCornerShape(8.dp))
            .clickable { onCheckedChange(!checked) }
            .padding(horizontal = 14.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = if (checked) Accent else Color(0xFFCCCCCC), fontSize = 13.sp)
    }
}

/**
 * 变速调节面板
 */
@Composable
fun SpeedPanel(current: Float, onSpeedChange: (Float) -> Unit) {
    val speeds = listOf(0.25f to "0.25x", 0.5f to "0.5x", 1f to "1x", 1.5f to "1.5x", 2f to "2x", 4f to "4x")
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        PanelTitle("变速")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            speeds.forEach { (speed, label) ->
                val isActive = current == speed
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .height(38.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (isActive) Accent else Color(0xFF1E1E1E))
                        .border(1.dp, if (isActive) Accent else Color(0xFF333333), RoundedCornerShape(8.dp))
                        .clickable { onSpeedChange(speed) },
                    contentAlignment = Alignment.Center
                ) {
                    Text(label, color = if (isActive) Color.Black else Color.White, fontSize = 12.sp, fontWeight = FontWeight.Bold)
                }
            }
        }
        Text("后续可扩展曲线变速、卡点变速和慢动作补帧。", color = Color(0xFF777777), fontSize = 11.sp)
    }
}

/**
 * 转场属性面板
 */
@Composable
fun TransitionPanel(timelineVm: TimelineViewModel) {
    val transitions = listOf(
        "无" to TimelineManager.TransitionType.NONE,
        "叠化" to TimelineManager.TransitionType.CROSSFADE,
        "闪白" to TimelineManager.TransitionType.FLASH,
        "左擦除" to TimelineManager.TransitionType.WIPE_LEFT,
        "左滑入" to TimelineManager.TransitionType.SLIDE_LEFT,
        "放大" to TimelineManager.TransitionType.ZOOM_IN,
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
                ToggleChip(label = label, checked = selected == type) {
                    selected = type
                    timelineVm.setTransition(type, 500_000L)
                }
            }
        }
    }
}

/**
 * 特效快捷面板
 */
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
        Text("展示 SDK 已规划的实时滤镜、道具贴纸、妆容和分割能力。", color = Color(0xFF888888), fontSize = 12.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf("电影感", "发光描边", "背景分割", "贴纸跟随").forEach { name ->
                ToggleChip(label = name, checked = false) {}
            }
        }
    }
}

/**
 * 文字工具入口面板
 */
@Composable
fun TextToolPanel(onNavigateToText: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(PanelBg)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        PanelTitle("文字与字幕")
        Button(
            onClick = onNavigateToText,
            modifier = Modifier.fillMaxWidth(),
            colors = ButtonDefaults.buttonColors(containerColor = Accent),
            shape = RoundedCornerShape(10.dp)
        ) {
            Icon(Icons.Filled.Add, contentDescription = null, tint = Color.Black, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("添加标题 / 字幕", color = Color.Black, fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun PanelTitle(title: String) {
    Text(title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
}
