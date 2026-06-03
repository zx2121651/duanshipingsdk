package com.sdk.video.sample.core.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * 带有名称标签和实时数值显示的参数滑动条 (ParameterSlider)
 * 
 * 此组件是 SDK UI 规范中使用的标准定制 Slider，采用了：
 * - 白色的滑块 (Thumb) 视觉焦点
 * - 荧光青色 (#00D7FF) 的已滑动发光轨道 (Active Track)
 * - 深灰暗色 (#222226) 的未滑动背景轨道 (Inactive Track)
 *
 * @param label 参数显示的名称，例如 "亮度"、"磨皮" 等
 * @param value 当前滑动条的浮点数值
 * @param onValueChange 滑动数值变化时的回调 Lambda 监听器
 * @param valueRange 可滑动数值的起止区间，默认 0.0 到 1.0
 * @param formatValue 数值文本格式化函数，默认保留两位小数
 * @param modifier 外部布局修饰符
 */
@Composable
fun ParameterSlider(
    label: String,
    value: Float,
    onValueChange: (Float) -> Unit,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    formatValue: (Float) -> String = { "%.2f".format(it) },
    enabled: Boolean = true,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier.padding(vertical = 6.dp)) {
        // 顶部文字区：左侧名称，右侧当前精确数值
        Row(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
            Text(
                text = label, 
                color = Color(0xFFCCCCCC), 
                fontSize = 13.sp, 
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.weight(1f)
            )
            Text(
                text = formatValue(value), 
                color = Color(0xFF00D7FF), 
                fontSize = 13.sp, 
                fontWeight = FontWeight.Bold
            )
        }
        // Compose 标准滑动条组件，应用剪映标准暗黑发光色彩设计
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = valueRange,
            enabled = enabled,
            colors = SliderDefaults.colors(
                thumbColor = if (enabled) Color.White else Color(0xFF555555),
                activeTrackColor = if (enabled) Color(0xFF00D7FF) else Color(0xFF333333),
                inactiveTrackColor = Color(0xFF222226)
            ),
            modifier = Modifier.padding(horizontal = 16.dp)
        )
    }
}
