package com.sdk.video.sample.feature.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import com.sdk.video.sample.core.model.TimelineViewModel

private val Orange = Color(0xFFFF6B00)
private val SurfaceDark = Color(0xFF1A1A1A)

private val PRESET_COLORS = listOf(
    0xFFFFFFFF, 0xFFFF6B00, 0xFFFFD700, 0xFF4CAF50,
    0xFF2196F3, 0xFFE91E63, 0xFF9C27B0, 0xFF000000
).map { Color(it) }

private val ANIMATION_OPTIONS = listOf("无", "淡入", "滑入", "弹跳", "打字机", "放大")

/**
 * 文本/字幕编辑器组件 (TextEditorScreen)
 *
 * 核心逻辑与功能:
 * 1. 提供了剪映式的字幕参数配置面板，包含：文本内容输入、字号滑动调节、颜色预设选取。
 * 2. 支持加粗、斜体、描边和阴影等样式配置组合。
 * 3. 接入了入场动画特效选项以及基于时间轴的可视化时间段微调滑块。
 * 4. 确认后将数据异步持久化至共享的 [TimelineViewModel] 中，完成多轨时间线的字幕渲染帧挂载。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TextEditorScreen(
    navController: NavController,
    timelineVm: TimelineViewModel
) {
    var inputText      by remember { mutableStateOf("") }
    var fontSize       by remember { mutableStateOf(28f) }
    var selectedColor  by remember { mutableStateOf(PRESET_COLORS[0]) }
    var isBold         by remember { mutableStateOf(false) }
    var isItalic       by remember { mutableStateOf(false) }
    var hasStroke      by remember { mutableStateOf(false) }
    var hasShadow      by remember { mutableStateOf(true) }
    var startSec       by remember { mutableStateOf(0f) }
    var endSec         by remember { mutableStateOf(3f) }
    var selectedAnim   by remember { mutableStateOf(0) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("文字 / 字幕", color = Color.White, fontWeight = FontWeight.Bold) },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(Icons.Filled.Close, contentDescription = "关闭", tint = Color.White)
                    }
                },
                actions = {
                    TextButton(
                        onClick = {
                            if (inputText.isNotBlank()) {
                                timelineVm.addTextClip(
                                    text       = inputText,
                                    startUs    = (startSec * 1_000_000f).toLong(),
                                    endUs      = (endSec * 1_000_000f).toLong(),
                                    fontSize   = fontSize,
                                    colorArgb  = selectedColor.value.toLong()
                                )
                                navController.popBackStack()
                            }
                        },
                        enabled = inputText.isNotBlank()
                    ) {
                        Text(
                            "确认",
                            color = if (inputText.isNotBlank()) Orange else Color(0xFF555555),
                            fontWeight = FontWeight.Bold,
                            fontSize = 15.sp
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color(0xFF111111))
            )
        },
        containerColor = Color(0xFF0D0D0D)
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
        ) {
            TextPreviewBox(
                text    = inputText.ifBlank { "点击下方输入文字…" },
                color   = if (inputText.isNotBlank()) selectedColor else Color(0xFF555555),
                size    = fontSize,
                bold    = isBold,
                italic  = isItalic,
                shadow  = hasShadow
            )

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                OutlinedTextField(
                    value = inputText,
                    onValueChange = { inputText = it },
                    placeholder = { Text("输入文字内容…", color = Color(0xFF555555)) },
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor   = Orange,
                        unfocusedBorderColor = Color(0xFF333333),
                        focusedTextColor     = Color.White,
                        unfocusedTextColor   = Color.White,
                        cursorColor          = Orange
                    ),
                    textStyle = TextStyle(fontSize = 15.sp),
                    maxLines = 3
                )

                SectionLabel("字号")
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("小", color = Color(0xFF888888), fontSize = 12.sp)
                    Slider(
                        value = fontSize,
                        onValueChange = { fontSize = it },
                        valueRange = 12f..72f,
                        modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
                        colors = SliderDefaults.colors(thumbColor = Orange, activeTrackColor = Orange)
                    )
                    Text("大", color = Color(0xFF888888), fontSize = 12.sp)
                    Spacer(Modifier.width(8.dp))
                    Text("${fontSize.toInt()}sp", color = Orange, fontSize = 12.sp, modifier = Modifier.width(36.dp))
                }

                SectionLabel("颜色")
                LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    items(PRESET_COLORS) { color ->
                        Box(
                            modifier = Modifier
                                .size(32.dp)
                                .clip(CircleShape)
                                .background(color)
                                .then(
                                    if (selectedColor == color)
                                        Modifier.border(2.dp, Color.White, CircleShape)
                                    else Modifier.border(1.dp, Color(0xFF444444), CircleShape)
                                )
                                .clickable { selectedColor = color }
                        )
                    }
                }

                SectionLabel("样式")
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    StyleToggle(label = "粗体",  active = isBold)   { isBold = it }
                    StyleToggle(label = "斜体",  active = isItalic) { isItalic = it }
                    StyleToggle(label = "描边",  active = hasStroke) { hasStroke = it }
                    StyleToggle(label = "阴影",  active = hasShadow) { hasShadow = it }
                }

                SectionLabel("入场动画")
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(ANIMATION_OPTIONS.mapIndexed { i, s -> i to s }) { (i, name) ->
                        AnimChip(name = name, selected = selectedAnim == i) { selectedAnim = i }
                    }
                }

                SectionLabel("时间段（秒）")
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    TimingSlider(
                        label = "开始",
                        value = startSec,
                        range = 0f..60f
                    ) { startSec = it.coerceAtMost(endSec - 0.5f) }
                    TimingSlider(
                        label = "结束",
                        value = endSec,
                        range = 0f..60f
                    ) { endSec = it.coerceAtLeast(startSec + 0.5f) }
                    Text(
                        "时长: ${String.format("%.1f", endSec - startSec)} 秒",
                        color = Color(0xFF888888),
                        fontSize = 12.sp
                    )
                }

                Spacer(Modifier.height(24.dp))
            }
        }
    }
}

@Composable
private fun TextPreviewBox(
    text: String,
    color: Color,
    size: Float,
    bold: Boolean,
    italic: Boolean,
    shadow: Boolean
) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(140.dp)
            .padding(horizontal = 20.dp, vertical = 12.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0xFF1A1A1A)),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = text,
            color = color,
            fontSize = size.sp,
            fontWeight = if (bold) FontWeight.Bold else FontWeight.Normal,
            fontStyle  = if (italic) FontStyle.Italic else FontStyle.Normal,
            textAlign  = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 16.dp)
        )
    }
}

@Composable
private fun SectionLabel(label: String) {
    Text(label, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
}

@Composable
private fun StyleToggle(label: String, active: Boolean, onToggle: (Boolean) -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (active) Orange.copy(alpha = 0.15f) else Color(0xFF1E1E1E))
            .border(1.dp, if (active) Orange else Color(0xFF333333), RoundedCornerShape(8.dp))
            .clickable { onToggle(!active) }
            .padding(horizontal = 16.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = if (active) Orange else Color(0xFFCCCCCC), fontSize = 13.sp,
             fontWeight = if (active) FontWeight.Bold else FontWeight.Normal)
    }
}

@Composable
private fun AnimChip(name: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (selected) Orange else Color(0xFF1E1E1E))
            .border(1.dp, if (selected) Orange else Color(0xFF333333), RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(name, color = Color.White, fontSize = 13.sp)
    }
}

@Composable
private fun TimingSlider(label: String, value: Float, range: ClosedFloatingPointRange<Float>, onValueChange: (Float) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = Color(0xFFCCCCCC), fontSize = 13.sp, modifier = Modifier.width(36.dp))
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = range,
            modifier = Modifier.weight(1f),
            colors = SliderDefaults.colors(thumbColor = Orange, activeTrackColor = Orange)
        )
        Text("%.1fs".format(value), color = Color(0xFF888888), fontSize = 11.sp,
             modifier = Modifier.width(44.dp), textAlign = TextAlign.End)
    }
}
