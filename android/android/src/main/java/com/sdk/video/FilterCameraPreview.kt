package com.sdk.video

import android.opengl.GLSurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

@Composable
fun FilterCameraPreview(
    filterManager: VideoFilterManager,
    modifier: Modifier = Modifier
) {
    val engineState by filterManager.engineState.collectAsState()

    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        if (engineState == FilterEngineState.ERROR) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color(0xFF8B0000)),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    "Critical Engine Error. Please restart the app.",
                    color = Color.White,
                    modifier = Modifier.padding(16.dp)
                )
            }
        } else {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { context ->
                    val texIdHolder = java.util.concurrent.atomic.AtomicInteger(-1)
                    val ptsHolder = java.util.concurrent.atomic.AtomicLong(-1L)

                    GLSurfaceView(context).apply {
                        preserveEGLContextOnPause = true
                        filterManager.glThreadDispatcher = { runnable ->
                            queueEvent(runnable)
                            requestRender()
                        }
                        setEGLContextClientVersion(3)

                        val renderer = object : GLSurfaceView.Renderer {

                            val currentTextureId = texIdHolder

                            private var displayProgram = 0
                            private var positionHandle = -1
                            private var texCoordHandle = -1
                            private var samplerHandle = -1

                            private val vertexShaderCode =
                                "attribute vec4 vPosition; attribute vec2 vTexCoord; varying vec2 texCoord; void main() { gl_Position = vPosition; texCoord = vTexCoord; }"
                            private val fragmentShaderCode =
                                "precision mediump float; varying vec2 texCoord; uniform sampler2D sTexture; void main() { gl_FragColor = texture2D(sTexture, texCoord); }"

                            private var vertexBuffer: java.nio.FloatBuffer? = null
                            private var texBuffer: java.nio.FloatBuffer? = null

                            private fun loadShader(type: Int, shaderCode: String): Int {
                                val shader = GLES20.glCreateShader(type)
                                GLES20.glShaderSource(shader, shaderCode)
                                GLES20.glCompileShader(shader)
                                val compiled = IntArray(1)
                                GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, compiled, 0)
                                if (compiled[0] == 0) {
                                    val error = GLES20.glGetShaderInfoLog(shader)
                                    GLES20.glDeleteShader(shader)
                                    android.util.Log.e("FilterCameraPreview", "Shader compilation failed: $error")
                                    return 0
                                }
                                return shader
                            }

                            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                                // Task 3: Offload CameraX/Engine initialization to a background context.
                                // Although initialize() eventually runs on GL thread, the setup orchestration
                                // shouldn't block the SurfaceCreated callback which can be invoked by Main.
                                filterManager.scope.launch(Dispatchers.Default) {
                                    val res = filterManager.initialize()
                                    if (res.isFailure) {
                                        android.util.Log.e(
                                            "FilterCameraPreview",
                                            "Failed to initialize: ${res.exceptionOrNull()?.message}"
                                        )
                                    }
                                }

                                val vertexShader = loadShader(GLES20.GL_VERTEX_SHADER, vertexShaderCode)
                                val fragmentShader = loadShader(GLES20.GL_FRAGMENT_SHADER, fragmentShaderCode)
                                if (vertexShader == 0 || fragmentShader == 0) return

                                displayProgram = GLES20.glCreateProgram()
                                GLES20.glAttachShader(displayProgram, vertexShader)
                                GLES20.glAttachShader(displayProgram, fragmentShader)
                                GLES20.glLinkProgram(displayProgram)
                                val linked = IntArray(1)
                                GLES20.glGetProgramiv(displayProgram, GLES20.GL_LINK_STATUS, linked, 0)
                                if (linked[0] == 0) {
                                    android.util.Log.e("FilterCameraPreview",
                                        "Program link failed: ${GLES20.glGetProgramInfoLog(displayProgram)}")
                                    displayProgram = 0
                                    return
                                }

                                positionHandle = GLES20.glGetAttribLocation(displayProgram, "vPosition")
                                texCoordHandle = GLES20.glGetAttribLocation(displayProgram, "vTexCoord")
                                samplerHandle = GLES20.glGetUniformLocation(displayProgram, "sTexture")

                                val squareCoords = floatArrayOf(-1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f)
                                val textureCoords = floatArrayOf(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f)

                                vertexBuffer = java.nio.ByteBuffer.allocateDirect(squareCoords.size * 4)
                                    .order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().apply {
                                    put(squareCoords).position(0)
                                }
                                texBuffer = java.nio.ByteBuffer.allocateDirect(textureCoords.size * 4)
                                    .order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().apply {
                                    put(textureCoords).position(0)
                                }
                            }

                            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                                gl?.glViewport(0, 0, width, height)
                            }

                            override fun onDrawFrame(gl: GL10?) {
                                GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
                                GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
                                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)

                                val texId = currentTextureId.get()
                                if (texId >= 0 && displayProgram != 0) {
                                    GLES20.glUseProgram(displayProgram)

                                    vertexBuffer?.let {
                                        GLES20.glEnableVertexAttribArray(positionHandle)
                                        GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 0, it)
                                    }
                                    texBuffer?.let {
                                        GLES20.glEnableVertexAttribArray(texCoordHandle)
                                        GLES20.glVertexAttribPointer(texCoordHandle, 2, GLES20.GL_FLOAT, false, 0, it)
                                    }

                                    GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
                                    GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texId)
                                    GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
                                    GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
                                    GLES20.glUniform1i(samplerHandle, 0)

                                    GLES20.glDrawArrays(GLES20.GL_TRIANGLE_FAN, 0, 4)

                                    GLES20.glDisableVertexAttribArray(positionHandle)
                                    GLES20.glDisableVertexAttribArray(texCoordHandle)

                                    val pts = ptsHolder.get()
                                    if (pts >= 0L && filterManager.isRecording()) {
                                        filterManager.renderToRecordingSurface(texId, pts)
                                    }

                                    filterManager.markFrameRendered(texId)
                                }
                            }
                        }
                        setRenderer(renderer)
                        renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY

                        // 直接回调：native 每处理完一帧（GL 线程），同步通知显示层
                        // 绕过协程/SharedFlow，彻底消除调度延迟和 detach 丢帧风险
                        filterManager.onFrameOutput = { texId, timestampNs ->
                            texIdHolder.set(texId)
                            ptsHolder.set(timestampNs)
                            requestRender()
                        }
                    }
                }
            )
        }

        if (engineState == FilterEngineState.DEGRADED) {
            Box(modifier = Modifier.fillMaxSize().padding(16.dp), contentAlignment = Alignment.TopCenter) {
                Text("Warning: Engine degraded. Some filters may be disabled.", color = Color.Yellow)
            }
        }
    }
}
