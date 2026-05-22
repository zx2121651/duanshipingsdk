package com.sdk.video.sample.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Analytics
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
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
import com.sdk.video.sample.feature.import.ImportScreen
import com.sdk.video.sample.feature.editor.TimelineEditorScreen
import com.sdk.video.sample.feature.editor.TextEditorScreen
import com.sdk.video.sample.feature.export.ExportScreen

private val fullscreenRoutes = setOf("import", "timeline_editor", "export", "text_editor")

private data class NavItem(
    val route: String,
    val label: String,
    val icon: androidx.compose.ui.graphics.vector.ImageVector
)

private val navItems = listOf(
    NavItem("home", "首页", Icons.Filled.Home),
    NavItem("capture", "拍摄", Icons.Filled.Videocam),
    NavItem("effects", "特效", Icons.Filled.ContentCut),
    NavItem("diagnostics", "设置", Icons.Filled.Analytics),
)

@Composable
fun HomeScaffold(appViewModel: AppViewModel) {
    val navController = rememberNavController()
    val timelineVm: TimelineViewModel = viewModel()
    val backStack by navController.currentBackStackEntryAsState()
    val currentRoute = backStack?.destination?.route
    val showBottomBar = currentRoute !in fullscreenRoutes

    Scaffold(
        bottomBar = {
            AnimatedVisibility(
                visible = showBottomBar,
                enter = slideInVertically { it },
                exit = slideOutVertically { it }
            ) {
                NavigationBar(containerColor = Color(0xFF101010)) {
                    navItems.forEach { item ->
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
                            },
                            icon = { Icon(item.icon, contentDescription = item.label) },
                            label = { Text(item.label) }
                        )
                    }
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
                composable("capture") { CaptureScreen(appViewModel) }
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
