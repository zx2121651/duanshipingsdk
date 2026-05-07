package com.sdk.video.sample.ui

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sdk.video.sample.state.AppViewModel
import org.json.JSONObject
import java.io.File

private data class EffectMeta(
    val id: String,
    val name: String,
    val type: String,
    val effectRoot: String  // 绝对路径，已复制到 filesDir
)

/**
 * 特效页：从 assets/effects/ 拷贝 manifest 到 filesDir，列表展示并支持激活/停用。
 */
@Composable
fun EffectsScreen(viewModel: AppViewModel) {
    val context     = LocalContext.current
    val activeId    by viewModel.activeEffectId.collectAsState()
    var effects     by remember { mutableStateOf<List<EffectMeta>>(emptyList()) }
    var loadError   by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        try {
            effects = copyAssetsToFilesDir(context)
        } catch (e: Exception) {
            loadError = e.message
        }
    }

    Column(modifier = Modifier.fillMaxSize().background(Color.Black)) {
        Text(
            "特效包",
            color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)
        )

        loadError?.let {
            Text("加载失败：$it", color = Color(0xFFFF6B6B),
                 modifier = Modifier.padding(16.dp))
        }

        if (effects.isEmpty() && loadError == null) {
            Text("正在解压特效…", color = Color(0xFF888888),
                 modifier = Modifier.padding(16.dp))
        }

        // ── Effects grid ───────────────────────────────────────
        LazyVerticalGrid(
            columns = GridCells.Fixed(2),
            contentPadding = PaddingValues(12.dp),
            modifier = Modifier.weight(1f)
        ) {
            items(effects) { meta ->
                val selected = meta.id == activeId
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = if (selected) Color(0xFF2A4A2A) else Color(0xFF1A1A1A)
                    ),
                    modifier = Modifier
                        .padding(6.dp)
                        .fillMaxWidth()
                        .height(140.dp)
                        .clickable {
                            if (selected) {
                                viewModel.activateEffect(null)
                            } else {
                                viewModel.loadAndActivateEffect(meta.effectRoot, meta.id)
                            }
                        }
                ) {
                    Column(
                        modifier = Modifier.fillMaxSize().padding(12.dp),
                        verticalArrangement = Arrangement.SpaceBetween
                    ) {
                        Box(
                            modifier = Modifier
                                .size(48.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(Color(0xFF333333)),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(Icons.Filled.AutoFixHigh, contentDescription = null,
                                 tint = if (selected) Color(0xFF4ADE80) else Color.White)
                        }
                        Column {
                            Text(meta.name, color = Color.White,
                                 fontSize = 14.sp, fontWeight = FontWeight.Bold)
                            Text(meta.type, color = Color(0xFF888888), fontSize = 11.sp)
                            Text("id: ${meta.id}", color = Color(0xFF666666), fontSize = 10.sp)
                        }
                        if (selected) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Icon(Icons.Filled.CheckCircle, contentDescription = null,
                                     tint = Color(0xFF4ADE80), modifier = Modifier.size(14.dp))
                                Spacer(Modifier.width(4.dp))
                                Text("已激活", color = Color(0xFF4ADE80), fontSize = 11.sp)
                            }
                        }
                    }
                }
            }
        }

        // ── Bottom: deactivate-all button ──────────────────────
        Button(
            onClick = { viewModel.activateEffect(null) },
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF333333)),
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp)
        ) {
            Text("停用全部特效", color = Color.White)
        }
    }
}

/**
 * 启动时把 assets/effects/<id>/manifest.json 复制到 filesDir/effects/<id>/manifest.json，
 * 因为底层 EffectPluginManager 使用 std::ifstream 读盘，无法直接读 APK 内的 asset。
 */
private fun copyAssetsToFilesDir(context: Context): List<EffectMeta> {
    val am   = context.assets
    val root = File(context.filesDir, "effects").apply { mkdirs() }
    val ids  = am.list("effects") ?: emptyArray()
    val out  = mutableListOf<EffectMeta>()

    for (id in ids) {
        val srcManifest = "effects/$id/manifest.json"
        val dstDir      = File(root, id).apply { mkdirs() }
        val dstManifest = File(dstDir, "manifest.json")

        am.open(srcManifest).use { input ->
            dstManifest.outputStream().use { output -> input.copyTo(output) }
        }

        // Parse manifest to extract name + type for the card
        val json = dstManifest.readText()
        val obj  = JSONObject(json)
        out.add(EffectMeta(
            id         = obj.optString("id", id),
            name       = obj.optString("name", id),
            type       = obj.optString("type", "unknown"),
            effectRoot = dstDir.absolutePath
        ))
    }
    return out
}
