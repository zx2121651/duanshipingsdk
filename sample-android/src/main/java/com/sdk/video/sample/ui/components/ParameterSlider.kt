package com.sdk.video.sample.ui.components

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
 * 带标签的参数 Slider。
 */
@Composable
fun ParameterSlider(
    label: String,
    value: Float,
    onValueChange: (Float) -> Unit,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    formatValue: (Float) -> String = { "%.2f".format(it) },
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier.padding(vertical = 6.dp)) {
        Row(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
            Text(label, color = Color(0xFFCCCCCC), fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                 modifier = Modifier.weight(1f))
            Text(formatValue(value), color = Color(0xFF00D7FF), fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = valueRange,
            colors = SliderDefaults.colors(
                thumbColor = Color.White,
                activeTrackColor = Color(0xFF00D7FF),
                inactiveTrackColor = Color(0xFF222226)
            ),
            modifier = Modifier.padding(horizontal = 16.dp)
        )
    }
}
