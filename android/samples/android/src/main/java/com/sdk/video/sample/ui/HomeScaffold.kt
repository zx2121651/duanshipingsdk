package com.sdk.video.sample.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.FloatingActionButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.sdk.video.sample.core.model.AppViewModel
import com.sdk.video.sample.core.model.TimelineViewModel
import com.sdk.video.sample.feature.home.HomeScreen
import com.sdk.video.sample.feature.capture.CaptureScreen
import com.sdk.video.sample.feature.effects.EffectsScreen
import com.sdk.video.sample.feature.diagnostics.DiagnosticsScreen
import com.sdk.video.sample.feature.mediaimport.ImportScreen
import com.sdk.video.sample.feature.editor.TimelineEditorScreen
import com.sdk.video.sample.feature.editor.TextEditorScreen
import com.sdk.video.sample.feature.export.ExportScreen

/** 全屏路由不需要底部导航栏和录制按钮 */
private val fullscreenRoutes = setOf("import", "timeline_editor", "export", "text_editor", "capture")

/** 品牌主题色（荧光青，对标抖音/剪映） */
private val AccentCyan = Color(0xFF00D7FF)
private val BarBackground = Color(0xFF0A0A0A)
private val UnselectedColor = Color(0xFF888888)
private val FabRed = Color(0xFFEF4444)

private data class NavItem(
    val route: String,
    val label: String,
    val icon: androidx.compose.ui.graphics.vector.ImageVector
)

/** 底部导航栏：3 项（首页 / 特效 / 设置），中间留给录制按钮 */
private val navItems = listOf(
    NavItem("home",     "首页", Icons.Filled.Home),
    NavItem("effects",  "特效", Icons.Filled.AutoFixHigh),
    NavItem("diagnostics", "设置", Icons.Filled.Settings),
)

@Composable
fun HomeScaffold(appViewModel: AppViewModel) {
    val navController = rememberNavController()
    val timelineVm: TimelineViewModel = viewModel()
    val backStack by navController.currentBackStackEntryAsState()
    val currentRoute = backStack?.destination?.route
    val showBottomBar = currentRoute !in fullscreenRoutes

    Scaffold(
        // 全黑背景，防止底部栏动画退出 + 拍摄页动画进入的间隙暴露
        // Scaffold 默认浅色表面 → 白闪。将其设为深黑与 BarBackground 一致。
        containerColor = BarBackground,
        bottomBar = {
            AnimatedVisibility(
                visible = showBottomBar,
                enter = fadeIn(animationSpec = tween(250)),
                exit = fadeOut(animationSpec = tween(100))
            ) {
                NavigationBar(
                    containerColor = BarBackground,
                    tonalElevation = 0.dp
                ) {
                    // 布局策略：3 个导航项 + 2 个占位 Spacer（录制按钮下方）
                    // 索引 0, 1, 2 为实际导航项，中间留出录制按钮空间
                    for (index in 0..2) {
                        val item = navItems[index]
                        val selected = backStack?.destination?.hierarchy
                            ?.any { it.route == item.route } == true
                        NavigationBarItem(
                            selected = selected,
                            onClick = {
                                if (currentRoute != item.route) {
                                    navController.navigate(item.route) {
                                        popUpTo(navController.graph.startDestinationId) {
                                            saveState = true
                                        }
                                        launchSingleTop = true
                                        restoreState = true
                                    }
                                }
                                // TODO: 双击同一标签页时滚动至顶部（抖音交互）
                            },
                            icon = {
                                Icon(
                                    item.icon,
                                    contentDescription = item.label,
                                    modifier = Modifier.size(24.dp),
                                    tint = if (selected) AccentCyan else UnselectedColor
                                )
                            },
                            label = {
                                Text(
                                    item.label,
                                    color = if (selected) AccentCyan else UnselectedColor,
                                    fontSize = 11.sp,
                                    fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal
                                )
                            },
                            colors = NavigationBarItemDefaults.colors(
                                indicatorColor = Color.Transparent
                            )
                        )
                    }
                }
            }
        },
        // 中心录制按钮（抖音风格 [+] 按钮）
        // 仅在非全屏页面显示，覆盖在底部导航栏上方中央
        floatingActionButton = {
            AnimatedVisibility(
                visible = showBottomBar,
                enter = slideInVertically(initialOffsetY = { it }),
                exit = slideOutVertically(targetOffsetY = { it })
            ) {
                FloatingActionButton(
                    onClick = {
                        navController.navigate("capture") {
                            launchSingleTop = true
                        }
                    },
                    modifier = Modifier
                        .size(60.dp),
                    containerColor = FabRed,
                    contentColor = Color.White,
                    shape = CircleShape,
                    elevation = FloatingActionButtonDefaults.elevation(
                        defaultElevation = 6.dp,
                        pressedElevation = 10.dp
                    )
                ) {
                    Icon(
                        Icons.Filled.Videocam,
                        contentDescription = "录制",
                        modifier = Modifier.size(28.dp),
                        tint = Color.White
                    )
                }
            }
        }
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .background(Color.Black)
        ) {
            NavHost(
                navController = navController,
                startDestination = "home"
            ) {
                composable("home") { HomeScreen(navController, appViewModel, timelineVm) }
                composable("capture",
                    enterTransition = {
                        slideInVertically(
                            animationSpec = tween(350),
                            initialOffsetY = { it }
                        ) + fadeIn(animationSpec = tween(350))
                    },
                    exitTransition = {
                        slideOutVertically(
                            animationSpec = tween(300),
                            targetOffsetY = { it }
                        ) + fadeOut(animationSpec = tween(300))
                    },
                    popEnterTransition = { fadeIn(animationSpec = tween(250)) },
                    popExitTransition = {
                        slideOutVertically(
                            animationSpec = tween(300),
                            targetOffsetY = { it }
                        ) + fadeOut(animationSpec = tween(300))
                    }
                ) { CaptureScreen(appViewModel) }
                composable("effects") { EffectsScreen(appViewModel) }
                composable("diagnostics") { DiagnosticsScreen(appViewModel) }
                composable("import") { ImportScreen(navController, timelineVm) }
                composable("timeline_editor") { TimelineEditorScreen(navController, timelineVm, appViewModel) }
                composable("export") { ExportScreen(navController, timelineVm) }
                composable("text_editor") { TextEditorScreen(navController, timelineVm) }
            }
        }
    }
}
