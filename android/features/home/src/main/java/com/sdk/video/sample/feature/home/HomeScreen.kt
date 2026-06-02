@file:OptIn(ExperimentalMaterial3Api::class)

package com.sdk.video.sample.feature.home

import android.net.Uri
import android.widget.Toast
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
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
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController

// 从 core-model 核心数据库导入的共享 ViewModel
import com.sdk.video.sample.core.model.AppViewModel
import com.sdk.video.sample.core.model.TimelineViewModel

// 调色板常量（仿剪映极简暗色调与荧光青）
private val Accent = Color(0xFF00D7FF)       // 剪映经典荧光青色
private val AccentPurple = Color(0xFF8B5CFF) // AI紫色
private val AccentPink = Color(0xFFFF497C)   // 潮流粉色
private val SurfaceBg = Color(0xFF121214)     // 主板底背景色
private val SurfaceCard = Color(0xFF1E1E22)   // 深色卡片底板色
private val SurfaceLight = Color(0xFF2E2E36)  // 浅灰高亮底色
private val TextSecondary = Color(0xFF8E8E9F) // 辅助灰色文本
private val BorderColor = Color(0xFF2C2C32)   // 边框描边灰色

// 真实的草稿实体
data class DraftItem(
    val id: String,
    val title: String,
    val duration: String,
    val segmentCount: Int,
    val info: String,
    val timeStr: String,
    val accentColor: Color,
    val clipsCount: Int
)

// 全局内存级持久化草稿列表
private val globalDrafts = mutableStateListOf(
    DraftItem("draft_1", "2026 毕业季 · 纪念纪录片", "03:15", 12, "1080P · 30fps", "刚刚", Color(0xFF00D7FF), 3),
    DraftItem("draft_2", "Weekend Vlog · 露营日记", "01:45", 8, "4K · 60fps", "昨天", Color(0xFFFFB020), 2),
    DraftItem("draft_3", "国风卡点大片 · 剪辑工程", "00:30", 5, "1080P · 60fps", "3天前", Color(0xFF8B5CFF), 1)
)

// 快速工具项实体结构
private data class QuickEntry(
    val icon: ImageVector,
    val title: String,
    val subtitle: String,
    val tint: Color,
    val route: String
)

// 静态工具箱定义
private val quickEntries = listOf(
    QuickEntry(Icons.Filled.AutoAwesome, "AI一键成片", "智能卡点与剪辑", Color(0xFF8B5CFF), "import"),
    QuickEntry(Icons.Filled.Subtitles, "智能字幕", "语音识别一键匹配", Color(0xFF00C9A7), "text_editor"),
    QuickEntry(Icons.Filled.AutoFixHigh, "热门特效", "实时分割与滤镜", Color(0xFFFF497C), "effects"),
    QuickEntry(Icons.Filled.GraphicEq, "提词器", "边看提词边录制", Color(0xFFFFB020), "capture"),
    QuickEntry(Icons.Filled.Tune, "专业调色", "电影级色彩调节", Color(0xFF00BCD4), "timeline_editor"),
    QuickEntry(Icons.Filled.Speed, "曲线变速", "慢动作动作平滑", Color(0xFFFF7043), "timeline_editor"),
)

@Composable
fun HomeScreen(
    navController: NavController,
    appViewModel: AppViewModel,
    timelineVm: TimelineViewModel
) {
    val clips by timelineVm.clips.collectAsState()
    val context = LocalContext.current

    // 交互状态控制
    var activeDraftForSheet by remember { mutableStateOf<DraftItem?>(null) }
    var draftToRename by remember { mutableStateOf<DraftItem?>(null) }
    var showRenameDialog by remember { mutableStateOf(false) }
    var sheetState = rememberModalBottomSheetState()
    var showBottomSheet by remember { mutableStateOf(false) }

    // 主体容器
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(SurfaceBg)
    ) {
        // 顶部光晕装饰背景 (营造深邃微光感)
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(300.dp)
                .background(
                    Brush.radialGradient(
                        colors = listOf(AccentPurple.copy(alpha = 0.08f), Color.Transparent),
                        center = Offset(300f, 0f),
                        radius = 800f
                    )
                )
        )

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
        ) {
            HomeHeader(
                onSettingsClick = { navController.navigate("diagnostics") }
            )

            NewProjectHeroButton(
                onNewProject = { navController.navigate("import") },
                onCapture = { navController.navigate("capture") }
            )

            QuickToolGrid(
                onToolClick = { route, title ->
                    if (route == "capture" || route == "import" || route == "timeline_editor" || route == "text_editor" || route == "effects") {
                        navController.navigate(route)
                    } else {
                        Toast.makeText(context, "$title 功能已就绪，正在拉起 SDK...", Toast.LENGTH_SHORT).show()
                    }
                }
            )

            // 若当前核心数据层中已有载入的 clips，则在主页浮现“继续编辑”横幅
            if (clips.isNotEmpty()) {
                ContinueEditingBanner(
                    count = clips.size,
                    onClick = { navController.navigate("timeline_editor") }
                )
            }

            DraftListSection(
                drafts = globalDrafts,
                onOpenDraft = { draft ->
                    Toast.makeText(context, "正在载入草稿: ${draft.title}", Toast.LENGTH_SHORT).show()
                    // 模拟从草稿中导入 clip 片段
                    val mockUris = (0 until draft.clipsCount).map { i ->
                        Uri.parse("android.resource://${context.packageName}/raw/mock_video_$i")
                    }
                    timelineVm.importClips(mockUris, context)
                    navController.navigate("timeline_editor")
                },
                onMoreClick = { draft ->
                    activeDraftForSheet = draft
                    showBottomSheet = true
                },
                onImportNew = { navController.navigate("import") }
            )

            Spacer(Modifier.height(48.dp))
        }

        // 底部弹出板 BottomSheet（More操作菜单）
        if (showBottomSheet && activeDraftForSheet != null) {
            ModalBottomSheet(
                onDismissRequest = { showBottomSheet = false },
                sheetState = sheetState,
                containerColor = SurfaceCard,
                contentColor = Color.White,
                dragHandle = { BottomSheetDefaults.DragHandle(color = SurfaceLight) }
            ) {
                DraftActionMenu(
                    draft = activeDraftForSheet!!,
                    onRename = {
                        draftToRename = activeDraftForSheet
                        showRenameDialog = true
                        showBottomSheet = false
                    },
                    onDuplicate = {
                        val index = globalDrafts.indexOf(activeDraftForSheet)
                        if (index != -1) {
                            val original = activeDraftForSheet!!
                            val copy = original.copy(
                                id = "draft_${System.currentTimeMillis()}",
                                title = "${original.title} - 副本",
                                timeStr = "刚刚"
                            )
                            globalDrafts.add(index + 1, copy)
                            Toast.makeText(context, "复制副本成功", Toast.LENGTH_SHORT).show()
                        }
                        showBottomSheet = false
                    },
                    onDelete = {
                        globalDrafts.remove(activeDraftForSheet)
                        Toast.makeText(context, "已删除草稿", Toast.LENGTH_SHORT).show()
                        showBottomSheet = false
                    }
                )
            }
        }

        // 重命名对话框
        if (showRenameDialog && draftToRename != null) {
            var newName by remember { mutableStateOf(draftToRename!!.title) }
            AlertDialog(
                onDismissRequest = { showRenameDialog = false },
                containerColor = SurfaceCard,
                title = {
                    Text(
                        text = "重命名草稿",
                        color = Color.White,
                        fontSize = 18.sp,
                        fontWeight = FontWeight.Bold
                    )
                },
                text = {
                    Column {
                        OutlinedTextField(
                            value = newName,
                            onValueChange = { newName = it },
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedTextColor = Color.White,
                                unfocusedTextColor = Color.White,
                                focusedBorderColor = Accent,
                                unfocusedBorderColor = SurfaceLight,
                                cursorColor = Accent
                            ),
                            shape = RoundedCornerShape(12.dp),
                            singleLine = true,
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(top = 8.dp)
                        )
                    }
                },
                confirmButton = {
                    Button(
                        onClick = {
                            val index = globalDrafts.indexOfFirst { it.id == draftToRename!!.id }
                            if (index != -1 && newName.isNotBlank()) {
                                globalDrafts[index] = globalDrafts[index].copy(title = newName, timeStr = "刚刚")
                                Toast.makeText(context, "修改成功", Toast.LENGTH_SHORT).show()
                            }
                            showRenameDialog = false
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = Accent)
                    ) {
                        Text("确定", color = Color.Black, fontWeight = FontWeight.Bold)
                    }
                },
                dismissButton = {
                    TextButton(onClick = { showRenameDialog = false }) {
                        Text("取消", color = TextSecondary)
                    }
                }
            )
        }
    }
}

/**
 * 顶部 Header 组件
 */
@Composable
private fun HomeHeader(onSettingsClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .statusBarsPadding()
            .padding(horizontal = 20.dp, vertical = 20.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = "Effectone",
                    color = Color.White,
                    fontSize = 24.sp,
                    fontWeight = FontWeight.Black,
                    letterSpacing = 0.5.sp
                )
                Spacer(Modifier.width(6.dp))
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(
                            Brush.linearGradient(
                                colors = listOf(AccentPink, AccentPurple)
                            )
                        )
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = "剪映版",
                        color = Color.White,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
            }
            Text(
                text = "专业剪辑 · 智能特效 · 极致画质",
                color = TextSecondary,
                fontSize = 11.sp,
                fontWeight = FontWeight.Normal,
                modifier = Modifier.padding(top = 2.dp)
            )
        }

        // 设置与性能诊断入口
        IconButton(
            onClick = onSettingsClick,
            modifier = Modifier
                .size(40.dp)
                .clip(CircleShape)
                .background(SurfaceCard)
                .border(1.dp, BorderColor, CircleShape)
        ) {
            Icon(
                imageVector = Icons.Filled.Settings,
                contentDescription = "设置",
                tint = Color.White,
                modifier = Modifier.size(20.dp)
            )
        }
    }
}

/**
 * 流光渐变“开始创作” Hero 按钮 (高仿剪映主页核心)
 */
@Composable
private fun NewProjectHeroButton(
    onNewProject: () -> Unit,
    onCapture: () -> Unit
) {
    // 动态流光偏移计算
    val infiniteTransition = rememberInfiniteTransition(label = "hero_flow")
    val xOffset by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 5000, easing = LinearEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "xOffset"
    )

    // 压感缩放动效
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.96f else 1f,
        animationSpec = spring(dampingRatio = Spring.DampingRatioMediumBouncy, stiffness = Spring.StiffnessLow),
        label = "scale"
    )

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp)
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
            }
            .shadow(16.dp, RoundedCornerShape(24.dp))
            .clip(RoundedCornerShape(24.dp))
            .background(
                // 流光背景刷
                Brush.linearGradient(
                    colors = listOf(
                        Color(0xFF1E2942), 
                        Color(0xFF3B1E4A), 
                        Color(0xFF4A1E29), 
                        Color(0xFF1E2942)
                    ),
                    start = Offset(0f, 0f),
                    end = Offset(xOffset * 1000f, 1000f)
                )
            )
            .border(
                BorderStroke(
                    1.2.dp, 
                    Brush.linearGradient(
                        colors = listOf(Accent.copy(alpha = 0.6f), AccentPurple.copy(alpha = 0.6f))
                    )
                ), 
                RoundedCornerShape(24.dp)
            )
            .padding(24.dp)
    ) {
        Text(
            text = "开启你的高光时刻",
            color = Color.White,
            fontSize = 20.sp,
            fontWeight = FontWeight.ExtraBold
        )
        Text(
            text = "支持多轨时间线、4K无损导出和实时AI滤镜",
            color = TextSecondary,
            fontSize = 11.sp,
            modifier = Modifier.padding(top = 4.dp)
        )
        
        Spacer(Modifier.height(24.dp))

        Row(
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            // 开始创作卡片（经典流光大按钮）
            Button(
                onClick = onNewProject,
                interactionSource = interactionSource,
                modifier = Modifier
                    .weight(1.3f)
                    .height(58.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Color.Transparent),
                contentPadding = PaddingValues(),
                shape = RoundedCornerShape(16.dp)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(
                            Brush.linearGradient(
                                colors = listOf(AccentPurple, AccentPink)
                            )
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Filled.Add,
                            contentDescription = null,
                            tint = Color.White,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = "开始创作",
                            color = Color.White,
                            fontWeight = FontWeight.Black,
                            fontSize = 16.sp
                        )
                    }
                }
            }

            // 拍同款/相机录制
            OutlinedButton(
                onClick = onCapture,
                modifier = Modifier
                    .weight(1f)
                    .height(58.dp),
                border = BorderStroke(1.2.dp, SurfaceLight),
                shape = RoundedCornerShape(16.dp),
                colors = ButtonDefaults.outlinedButtonColors(
                    containerColor = SurfaceCard.copy(alpha = 0.5f),
                    contentColor = Color.White
                )
            ) {
                Icon(
                    imageVector = Icons.Filled.Videocam,
                    contentDescription = null,
                    tint = Color.White,
                    modifier = Modifier.size(20.dp)
                )
                Spacer(Modifier.width(6.dp))
                Text(
                    text = "拍摄",
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontSize = 15.sp
                )
            }
        }
    }
}

/**
 * 智能工具箱（金刚区）
 */
@Composable
private fun QuickToolGrid(onToolClick: (String, String) -> Unit) {
    Column(modifier = Modifier.padding(top = 28.dp)) {
        SectionHeader("智能工具箱", "对标剪映核心 SDK 能力")
        LazyRow(
            contentPadding = PaddingValues(horizontal = 20.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            items(quickEntries) { item ->
                Column(
                    modifier = Modifier
                        .width(120.dp)
                        .height(132.dp)
                        .clip(RoundedCornerShape(20.dp))
                        .background(SurfaceCard)
                        .border(1.dp, BorderColor, RoundedCornerShape(20.dp))
                        .clickable { onToolClick(item.route, item.title) }
                        .padding(14.dp),
                    verticalArrangement = Arrangement.SpaceBetween
                ) {
                    Box(
                        modifier = Modifier
                            .size(42.dp)
                            .clip(CircleShape)
                            .background(item.tint.copy(alpha = 0.12f))
                            .border(1.dp, item.tint.copy(alpha = 0.25f), CircleShape),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = item.icon,
                            contentDescription = item.title,
                            tint = item.tint,
                            modifier = Modifier.size(22.dp)
                        )
                    }
                    Column {
                        Text(
                            text = item.title,
                            color = Color.White,
                            fontSize = 13.sp,
                            fontWeight = FontWeight.Bold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Spacer(Modifier.height(2.dp))
                        Text(
                            text = item.subtitle,
                            color = TextSecondary,
                            fontSize = 10.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

/**
 * 继续上次编辑 Banner
 */
@Composable
private fun ContinueEditingBanner(count: Int, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 20.dp)
            .shadow(8.dp, RoundedCornerShape(16.dp))
            .clip(RoundedCornerShape(16.dp))
            .background(Color(0xFF142B30))
            .border(1.dp, Color(0xFF1C454C), RoundedCornerShape(16.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 18.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(36.dp)
                .clip(CircleShape)
                .background(Accent.copy(alpha = 0.2f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Filled.PlayArrow,
                contentDescription = null,
                tint = Accent,
                modifier = Modifier.size(24.dp)
            )
        }
        Spacer(Modifier.width(14.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "继续上次的视频剪辑",
                color = Color.White,
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = "时间线中已有 $count 个片段未导出",
                color = Accent.copy(alpha = 0.8f),
                fontSize = 11.sp,
                fontWeight = FontWeight.Medium
            )
        }
        Text(
            text = "恢复",
            color = Accent,
            fontSize = 13.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

/**
 * 草稿箱列表 Section
 */
@Composable
private fun DraftListSection(
    drafts: List<DraftItem>,
    onOpenDraft: (DraftItem) -> Unit,
    onMoreClick: (DraftItem) -> Unit,
    onImportNew: () -> Unit
) {
    Column(modifier = Modifier.padding(top = 22.dp)) {
        SectionHeader("最近项目", "本地自动保存，随时继续创作")

        if (drafts.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(150.dp),
                contentAlignment = Alignment.Center
            ) {
                Text("暂无草稿项目，点击下方开始创作", color = TextSecondary, fontSize = 13.sp)
            }
        } else {
            // 使用 animateItemPlacement 必须依赖 LazyColumn，不过因为这里嵌套在 Column(verticalScroll) 里，直接用 forEach 渲染卡片。
            // 使用 Column 的 animateContentSize 做添加删除动效。
            Column(modifier = Modifier.animateContentSize()) {
                drafts.forEach { draft ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 20.dp, vertical = 6.dp)
                            .clip(RoundedCornerShape(16.dp))
                            .background(SurfaceCard)
                            .border(1.dp, BorderColor, RoundedCornerShape(16.dp))
                            .clickable { onOpenDraft(draft) }
                            .padding(14.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        // 缩略图占位 (带渐变和微徽章)
                        Box(
                            modifier = Modifier
                                .size(88.dp, 58.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(
                                    Brush.linearGradient(
                                        colors = listOf(draft.accentColor.copy(alpha = 0.15f), SurfaceLight)
                                    )
                                )
                                .border(1.dp, BorderColor, RoundedCornerShape(8.dp)),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Filled.Movie,
                                contentDescription = null,
                                tint = draft.accentColor,
                                modifier = Modifier.size(24.dp)
                            )
                            Box(
                                modifier = Modifier
                                    .align(Alignment.BottomEnd)
                                    .padding(4.dp)
                                    .background(Color(0x99000000), RoundedCornerShape(4.dp))
                                    .padding(horizontal = 5.dp, vertical = 1.dp)
                            ) {
                                Text(
                                    text = draft.duration,
                                    color = Color.White,
                                    fontSize = 9.sp,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                        }

                        Spacer(Modifier.width(14.dp))

                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = draft.title,
                                color = Color.White,
                                fontSize = 14.sp,
                                fontWeight = FontWeight.Bold,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            Spacer(Modifier.height(4.dp))
                            Text(
                                text = "${draft.segmentCount} 个分段 · ${draft.info}",
                                color = TextSecondary,
                                fontSize = 11.sp
                            )
                            Spacer(Modifier.height(2.dp))
                            Text(
                                text = "修改时间: ${draft.timeStr}",
                                color = TextSecondary.copy(alpha = 0.7f),
                                fontSize = 10.sp
                            )
                        }

                        IconButton(onClick = { onMoreClick(draft) }) {
                            Icon(
                                imageVector = Icons.Filled.MoreVert,
                                contentDescription = "更多选项",
                                tint = TextSecondary,
                                modifier = Modifier.size(20.dp)
                            )
                        }
                    }
                }
            }
        }

        Spacer(Modifier.height(12.dp))

        // 新建工程导入连接
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp)
                .clip(RoundedCornerShape(14.dp))
                .border(1.dp, Accent.copy(alpha = 0.3f), RoundedCornerShape(14.dp))
                .clickable(onClick = onImportNew)
                .padding(vertical = 14.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Filled.PhotoLibrary,
                contentDescription = null,
                tint = Accent,
                modifier = Modifier.size(18.dp)
            )
            Spacer(Modifier.width(8.dp))
            Text(
                text = "导入本地素材，新建项目",
                color = Accent,
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold
            )
        }
    }
}

/**
 * 草稿箱 More 操作 BottomSheet 菜单
 */
@Composable
private fun DraftActionMenu(
    draft: DraftItem,
    onRename: () -> Unit,
    onDuplicate: () -> Unit,
    onDelete: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(bottom = 24.dp)
    ) {
        // 顶部展示当前选中的草稿名
        Text(
            text = draft.title,
            color = Color.White,
            fontSize = 15.sp,
            fontWeight = FontWeight.Bold,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 16.dp),
            textAlign = TextAlign.Center
        )

        Divider(color = SurfaceLight, thickness = 1.dp)

        // 重命名
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onRename() }
                .padding(horizontal = 24.dp, vertical = 16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Filled.Edit,
                contentDescription = null,
                tint = Color.White,
                modifier = Modifier.size(20.dp)
            )
            Spacer(Modifier.width(16.dp))
            Text("重命名", color = Color.White, fontSize = 15.sp)
        }

        // 复制副本
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onDuplicate() }
                .padding(horizontal = 24.dp, vertical = 16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Filled.ContentCopy,
                contentDescription = null,
                tint = Color.White,
                modifier = Modifier.size(20.dp)
            )
            Spacer(Modifier.width(16.dp))
            Text("复制副本", color = Color.White, fontSize = 15.sp)
        }

        // 删除草稿
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onDelete() }
                .padding(horizontal = 24.dp, vertical = 16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Filled.Delete,
                contentDescription = null,
                tint = AccentPink,
                modifier = Modifier.size(20.dp)
            )
            Spacer(Modifier.width(16.dp))
            Text("删除项目", color = AccentPink, fontSize = 15.sp, fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun SectionHeader(title: String, subtitle: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = title,
            color = Color.White,
            fontSize = 16.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(Modifier.width(10.dp))
        Text(
            text = subtitle,
            color = TextSecondary,
            fontSize = 11.sp,
            fontWeight = FontWeight.Normal
        )
    }
}
