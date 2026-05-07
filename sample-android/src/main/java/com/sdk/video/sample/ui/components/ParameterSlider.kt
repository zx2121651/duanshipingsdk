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
    Column(modifier = modifier.padding(vertical = 4.dp)) {
        Row(modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp)) {
            Text(label, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Medium,
                 modifier = Modifier.weight(1f))
            Text(formatValue(value), color = Color(0xFF4ADE80), fontSize = 13.sp)
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = valueRange,
            colors = SliderDefaults.colors(
                thumbColor = Color(0xFF4ADE80),
                activeTrackColor = Color(0xFF4ADE80),
                inactiveTrackColor = Color(0x33FFFFFF)
            ),
            modifier = Modifier.padding(horizontal = 12.dp)
        )
    }
}
