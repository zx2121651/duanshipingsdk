package com.sdk.video.sample.feature.effects

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.FaceRetouchingNatural
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sdk.video.sample.core.model.AppViewModel
import org.json.JSONObject
import java.io.File

// 抖音/剪映 经典科技感青色高亮
private val Accent = Color(0xFF00D7FF)

/**
 * 特效元数据结构体
 * @param id 特效包的唯一 ID
 * @param name 特效的展示名称
 * @param type 特效类型（例如 makeup 妆容、sticker 贴纸、filter 滤镜等）
 * @param effectRoot 特效资源在本地磁盘解压后的绝对路径目录，用于传递给 C++ 特效渲染器
 */
private data class EffectMeta(
    val id: String,
    val name: String,
    val type: String,
    val effectRoot: String
)

/**
 * 抖音/剪映特效中心控制面板 - Feature Effects Component
 * 
 * 核心逻辑与功能:
 * 1. 资源解密与释放：在 [LaunchedEffect] 中异步将 Assets 下的特效包释放到 App 私有沙盒目录中，
 *    以确保底层 C++ 特效渲染引擎有权限通过 POSIX File Reader 进行高性能资源加载。
 * 2. 描述解析：解析特效包内部的 `manifest.json` 配置文件，动态读取特效名称和分类。
 * 3. 实时切换控制：支持一键启用/禁用，事件通过 [AppViewModel.loadAndActivateEffect] 同步到底层。
 */
@Composable
fun EffectsScreen(viewModel: AppViewModel) {
    val context = LocalContext.current
    val activeId by viewModel.activeEffectId.collectAsState()
    var effects by remember { mutableStateOf<List<EffectMeta>>(emptyList()) }
    var loadError by remember { mutableStateOf<String?>(null) }

    // 页面挂载时自动解压特效包，对标正式 App 的下载/加载流程
    LaunchedEffect(Unit) {
        try {
            effects = copyAssetsToFilesDir(context)
        } catch (e: Exception) {
            loadError = e.message
        }
    }

    Column(modifier = Modifier.fillMaxSize().background(Color(0xFF0B0B0C))) {
        Column(modifier = Modifier.padding(horizontal = 20.dp, vertical = 18.dp)) {
            Text("特效中心", color = Color.White, fontSize = 22.sp, fontWeight = FontWeight.Bold)
            Text("对标抖音特效 SDK：滤镜、妆容、道具包、AI 分割。", color = Color(0xFF999999), fontSize = 12.sp)
        }

        loadError?.let {
            Text("特效加载失败：$it", color = Color(0xFFFF6B6B), modifier = Modifier.padding(20.dp))
        }

        if (effects.isEmpty() && loadError == null) {
            EmptyEffectsState()
        }

        LazyVerticalGrid(
            columns = GridCells.Fixed(2),
            contentPadding = PaddingValues(14.dp),
            modifier = Modifier.weight(1f),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            items(effects) { meta ->
                val selected = meta.id == activeId
                EffectCard(
                    meta = meta,
                    selected = selected,
                    onClick = {
                        if (selected) {
                            viewModel.activateEffect(null)
                        } else {
                            viewModel.loadAndActivateEffect(meta.effectRoot, meta.id)
                        }
                    }
                )
            }
        }

        Button(
            onClick = { viewModel.activateEffect(null) },
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF242424)),
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            shape = RoundedCornerShape(14.dp)
        ) {
            Text("关闭当前特效", color = Color.White, fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun EffectCard(meta: EffectMeta, selected: Boolean, onClick: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .height(150.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(if (selected) Accent.copy(alpha = 0.18f) else Color(0xFF181818))
            .clickable(onClick = onClick)
            .padding(14.dp),
        verticalArrangement = Arrangement.SpaceBetween
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                modifier = Modifier
                    .size(46.dp)
                    .clip(CircleShape)
                    .background(if (selected) Accent else Color(0xFF2A2A2A)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    if (meta.type.contains("makeup", ignoreCase = true)) Icons.Filled.FaceRetouchingNatural else Icons.Filled.AutoFixHigh,
                    contentDescription = null,
                    tint = if (selected) Color.Black else Accent,
                    modifier = Modifier.size(24.dp)
                )
            }
            Spacer(Modifier.weight(1f))
            if (selected) {
                Icon(Icons.Filled.CheckCircle, contentDescription = "已启用", tint = Accent, modifier = Modifier.size(20.dp))
            }
        }
        Column {
            Text(meta.name, color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.Bold)
            Text(meta.type, color = Color(0xFF999999), fontSize = 11.sp)
            Text("包 ID：${meta.id}", color = Color(0xFF666666), fontSize = 10.sp)
        }
    }
}

@Composable
private fun EmptyEffectsState() {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(Icons.Filled.AutoAwesome, contentDescription = null, tint = Color(0xFF444444), modifier = Modifier.size(64.dp))
        Spacer(Modifier.height(12.dp))
        Text("正在加载本地特效包", color = Color(0xFFAAAAAA), fontSize = 14.sp)
    }
}

/**
 * 辅助函数：将 Assets 中打包好的特效配置复制释放到系统 App Files 目录
 * 以便于 C++ native 层文件读取组件直接读取绝对文件路径
 */
private fun copyAssetsToFilesDir(context: Context): List<EffectMeta> {
    val assetManager = context.assets
    val root = File(context.filesDir, "effects").apply { mkdirs() }
    val ids = assetManager.list("effects") ?: emptyArray()
    val out = mutableListOf<EffectMeta>()

    for (id in ids) {
        val srcManifest = "effects/$id/manifest.json"
        val dstDir = File(root, id).apply { mkdirs() }
        val dstManifest = File(dstDir, "manifest.json")

        assetManager.open(srcManifest).use { input ->
            dstManifest.outputStream().use { output -> input.copyTo(output) }
        }

        val obj = JSONObject(dstManifest.readText())
        out.add(
            EffectMeta(
                id = obj.optString("id", id),
                name = obj.optString("name", id),
                type = obj.optString("type", "unknown"),
                effectRoot = dstDir.absolutePath
            )
        )
    }
    return out
}
