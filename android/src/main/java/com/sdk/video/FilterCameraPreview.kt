package com.sdk.video

import android.opengl.GLSurfaceView
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
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

    // Handle initialization and release based on the Composable's lifecycle
    DisposableEffect(filterManager) {
        scope.launch {
            filterManager.initialize()
        }
        onDispose {
            scope.launch {
                filterManager.release()
            }
        }
    }

    Box(modifier = modifier, contentAlignment = Alignment.Center) {
        if (engineState == FilterEngineState.ERROR) {
            // Degraded UI state when the engine fails completely
            Text("Camera failed. Degrading to safe mode...", color = Color.Red)
        } else {
            AndroidView(
                modifier = modifier,
                factory = { context ->
                    GLSurfaceView(context).apply {
                        setEGLContextClientVersion(3)
                        setRenderer(object : GLSurfaceView.Renderer {
                            var currentTextureId = -1

                            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                                // Collect the processed texture IDs from the SharedFlow
                                scope.launch {
                                    filterManager.processedFrames.collect { result ->
                                        result.onSuccess { texId ->
                                            currentTextureId = texId
                                            requestRender() // Trigger a draw frame
                                        }.onFailure {
                                            // Handling degradation per frame
                                            // Can optionally fallback to render raw input if needed
                                        }
                                    }
                                }
                            }

                            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                                gl?.glViewport(0, 0, width, height)
                            }

                            override fun onDrawFrame(gl: GL10?) {
                                if (currentTextureId >= 0 && engineState != FilterEngineState.DEGRADED) {
                                    // Normally, we'd bind a simple screen shader here to draw the OES/2D texture
                                    // Example: SimpleRenderer.draw(currentTextureId)
                                    // For now, clear the screen
                                    gl?.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
                                    gl?.glClear(GL10.GL_COLOR_BUFFER_BIT)
                                } else {
                                    // Degraded rendering
                                    gl?.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
                                    gl?.glClear(GL10.GL_COLOR_BUFFER_BIT)
                                }
                            }
                        })
                        // Render only when a new frame is produced
                        renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
                    }
                }
            )
        }
    }
}
