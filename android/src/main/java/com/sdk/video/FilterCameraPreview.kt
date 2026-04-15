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
import android.opengl.GLES20
import kotlinx.coroutines.launch

@Composable
fun FilterCameraPreview(
    filterManager: VideoFilterManager,
    modifier: Modifier = Modifier
) {
    val engineState by filterManager.engineState.collectAsState()
    val scope = rememberCoroutineScope()

    // Intensity State for the ComputeBlurFilter
    var intensity by remember { mutableFloatStateOf(1.0f) }

    var glView by remember { mutableStateOf<GLSurfaceView?>(null) }

    // React to Slider changes
    LaunchedEffect(intensity) {
        scope.launch {
            filterManager.updateParameter("blurSize", intensity * 10f)
        }
    }

    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        if (engineState == FilterEngineState.ERROR) {
            Text("Critical Engine Error. Please restart the app.", color = Color.Red)
        } else {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { context ->
                    GLSurfaceView(context).apply {
                        glView = this
                        filterManager.glThreadDispatcher = { runnable -> queueEvent(runnable) }
                        setEGLContextClientVersion(3)

                        val renderer = object : GLSurfaceView.Renderer {
                            var renderJob: kotlinx.coroutines.Job? = null

                            var currentTextureId = -1


                            private var displayProgram = 0
                            private val vertexShaderCode = "attribute vec4 vPosition; attribute vec2 vTexCoord; varying vec2 texCoord; void main() { gl_Position = vPosition; texCoord = vTexCoord; }"
                            private val fragmentShaderCode = "precision mediump float; varying vec2 texCoord; uniform sampler2D sTexture; void main() { gl_FragColor = texture2D(sTexture, texCoord); }"


                            private var vertexBuffer: java.nio.FloatBuffer? = null
                            private var texBuffer: java.nio.FloatBuffer? = null

                            private fun loadShader(type: Int, shaderCode: String): Int {
                                val shader = GLES20.glCreateShader(type)
                                GLES20.glShaderSource(shader, shaderCode)
                                GLES20.glCompileShader(shader)
                                return shader
                            }

                            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                                // ---- 【核心修复：GL 线程归属权】 ----
                                // 在 GLSurfaceView 分配的 GLThread 中初始化底层渲染引擎
                                // 这保证了底层 FBO、Texture 和 GLSurfaceView 永远在同一个上下文中
                                scope.launch {
                                    val res = filterManager.initialize()
                                    if (res.isFailure) {
                                        android.util.Log.e("FilterCameraPreview", "Failed to initialize: ${res.exceptionOrNull()?.message}")
                                    }
                                }
//                                 kotlinx.coroutines.GlobalScope.launch { filterManager.addFilter(VideoFilterType.COMPUTE_BLUR) }

                                val vertexShader = loadShader(GLES20.GL_VERTEX_SHADER, vertexShaderCode)
                                val fragmentShader = loadShader(GLES20.GL_FRAGMENT_SHADER, fragmentShaderCode)
                                displayProgram = GLES20.glCreateProgram().also {
                                    GLES20.glAttachShader(it, vertexShader)
                                    GLES20.glAttachShader(it, fragmentShader)
                                    GLES20.glLinkProgram(it)
                                }

                                val squareCoords = floatArrayOf(-1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f)
                                val textureCoords = floatArrayOf(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f)

                                vertexBuffer = java.nio.ByteBuffer.allocateDirect(squareCoords.size * 4).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().apply {
                                    put(squareCoords).position(0)
                                }
                                texBuffer = java.nio.ByteBuffer.allocateDirect(textureCoords.size * 4).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().apply {
                                    put(textureCoords).position(0)
                                }


                                renderJob?.cancel()
                                renderJob = filterManager.scope.launch {
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
                                // 触发 FilterEngine 在本 GL 线程消费 SurfaceTexture 数据
                                filterManager.processFrame()

                                GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)

                                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)

                                if (currentTextureId >= 0) {
                                    // Even if DEGRADED, we still want to show the frame (it might be the last successful one or a fallback)
                                    GLES20.glUseProgram(displayProgram)


                                    val positionHandle = GLES20.glGetAttribLocation(displayProgram, "vPosition")
                                    val texCoordHandle = GLES20.glGetAttribLocation(displayProgram, "vTexCoord")
                                    val samplerHandle = GLES20.glGetUniformLocation(displayProgram, "sTexture")

                                    vertexBuffer?.let {
                                        GLES20.glEnableVertexAttribArray(positionHandle)
                                        GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 0, it)
                                    }

                                    texBuffer?.let {
                                        GLES20.glEnableVertexAttribArray(texCoordHandle)
                                        GLES20.glVertexAttribPointer(texCoordHandle, 2, GLES20.GL_FLOAT, false, 0, it)
                                    }

                                    GLES20.glActiveTexture(GLES20.GL_TEXTURE0)

                                    GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, currentTextureId)
                                    GLES20.glUniform1i(samplerHandle, 0)

                                    GLES20.glDrawArrays(GLES20.GL_TRIANGLE_FAN, 0, 4)

                                    GLES20.glDisableVertexAttribArray(positionHandle)
                                    GLES20.glDisableVertexAttribArray(texCoordHandle)
                                }
                            }

                        }
                        setRenderer(renderer)
                        renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY

                        // Handle detaching to clean up the coroutine
                        addOnAttachStateChangeListener(object : android.view.View.OnAttachStateChangeListener {
                            override fun onViewAttachedToWindow(v: android.view.View) {}
                            override fun onViewDetachedFromWindow(v: android.view.View) {
                                renderer.renderJob?.cancel()
                            }
                        })
                    }
                }
            )
        }

        // Error/Warning Overlay
        if (engineState == FilterEngineState.DEGRADED) {
            Box(modifier = Modifier.fillMaxSize().padding(16.dp), contentAlignment = Alignment.TopCenter) {
                Text("Warning: Engine degraded. Some filters may be disabled.", color = Color.Yellow)
            }
        }

        // Overlay UI
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text("GLES 3.1 Compute Blur Radius: ${String.format("%.1f", intensity * 10f)}", color = Color.White)
            Slider(
                value = intensity,
                onValueChange = { intensity = it },
                valueRange = 0f..1f,
                modifier = Modifier.fillMaxWidth()
            )

            // Hidden/Debug Crash Simulator
            Button(onClick = {
                scope.launch {
                    filterManager.updateParameter("simulateCrash", 1f)
                }
            }) {
                Text("Simulate Overload (Test Degradation)")
            }
        }
    }
}
