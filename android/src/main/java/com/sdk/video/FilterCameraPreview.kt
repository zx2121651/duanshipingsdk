package com.sdk.video

import android.opengl.GLSurfaceView
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Button
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlinx.coroutines.launch

@Composable
fun FilterCameraPreview(
    filterManager: VideoFilterManager,
    modifier: Modifier = Modifier
) {
    val engineState by filterManager.engineState.collectAsState()
    val scope = rememberCoroutineScope()

    // Intensity State for the new CinematicLookupFilter
    var intensity by remember { mutableFloatStateOf(1.0f) }

    DisposableEffect(filterManager) {
        scope.launch {
            filterManager.initialize()
            // Add cinematic lookup by default for preview
            filterManager.addFilter(VideoFilterType.CINEMATIC_LOOKUP)
        }
        onDispose {
            scope.launch { filterManager.release() }
        }
    }

    // React to Slider changes
    LaunchedEffect(intensity) {
        filterManager.updateParameter("intensity", intensity)
    }

    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        if (engineState == FilterEngineState.ERROR) {
            Text("Camera failed. Degrading to safe mode...", color = Color.Red)
        } else {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { context ->
                    GLSurfaceView(context).apply {
                        setEGLContextClientVersion(3)
                        setRenderer(object : GLSurfaceView.Renderer {
                            var currentTextureId = -1

                            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                                scope.launch {
                                    filterManager.processedFrames.collect { result ->
                                        result.onSuccess { texId ->
                                            currentTextureId = texId
                                            requestRender()
                                        }
                                    }
                                }
                            }

                            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                                gl?.glViewport(0, 0, width, height)
                            }

                            override fun onDrawFrame(gl: GL10?) {
                                gl?.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
                                gl?.glClear(GL10.GL_COLOR_BUFFER_BIT)
                                // Normally draw currentTextureId here
                            }
                        })
                        renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
                    }
                }
            )
        }

        // Overlay UI
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text("Cinematic Intensity: ${String.format("%.2f", intensity)}", color = Color.White)
            Slider(
                value = intensity,
                onValueChange = { intensity = it },
                valueRange = 0f..1f,
                modifier = Modifier.fillMaxWidth()
            )

            // Hidden/Debug Crash Simulator
            Button(onClick = {
                scope.launch { filterManager.updateParameter("simulateCrash", 1f) }
            }) {
                Text("Simulate Overload (Test Degradation)")
            }
        }
    }
}
