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
import android.opengl.GLES30
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

                            // GLSurfaceView 真实像素尺寸，onSurfaceChanged 时更新，
                            // onDrawFrame 每帧用此恢复 viewport（native 管线会改 viewport）
                            private var surfaceWidth = 0
                            private var surfaceHeight = 0

                            // 显示层顶点着色器：简单 pass-through。
                            // Native OES2RGBFilter（src/Filters.cpp）已经在
                            // oes_to_rgb.vert 中应用了 SurfaceTexture 的
                            // transformMatrix（旋转/裁剪/镜像），输出到
                            // outputTexId 是已矫正的 GL_TEXTURE_2D。
                            // 显示层只需按原始纹理坐标采样即可，重复应用矩阵
                            // 会导致二次旋转 + 画面裁剪/偏移。
                            private val vertexShaderCode =
                                "attribute vec4 vPosition;" +
                                "attribute vec2 vTexCoord;" +
                                "varying vec2 texCoord;" +
                                "void main() {" +
                                "  gl_Position = vPosition;" +
                                "  texCoord = vTexCoord;" +
                                "}"
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
                                filterManager.glThreadId = Thread.currentThread().id
                                kotlinx.coroutines.runBlocking {
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

                                // FBO rendering (native OES2RGBFilter → output 2D texture) has
                                // Y inverted relative to the GLSurfaceView default framebuffer.
                                // Negate vertex Y to correct the vertical orientation.
                                val squareCoords = floatArrayOf(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f)
                                val textureCoords = floatArrayOf(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f)

                                vertexBuffer = java.nio.ByteBuffer.allocateDirect(squareCoords.size * 4)
                                    .order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().apply {
                                    put(squareCoords).position(0)
                                }
                                texBuffer = java.nio.ByteBuffer.allocateDirect(textureCoords.size * 4)
                                    .order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().apply {
                                    put(textureCoords).position(0)
                                }

                                // [FIX] Drain any GL errors left by display shader compilation.
                                // This is the last GL operation before camera frames start
                                // arriving.  Any stale error here would poison
                                // SurfaceTexture.updateTexImage() on the first frame.
                                var err = GLES20.glGetError()
                                while (err != GLES20.GL_NO_ERROR) {
                                    android.util.Log.w("FilterCameraPreview",
                                        "Display shader init left stale GL error: 0x${Integer.toHexString(err)}")
                                    err = GLES20.glGetError()
                                }
                            }

                            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                                surfaceWidth = width
                                surfaceHeight = height
                                GLES20.glViewport(0, 0, width, height)
                            }

                            override fun onDrawFrame(gl: GL10?) {
                                // ── 强制重置 GL 状态到显示层所需的基线 ──────────────────
                                // Native RHI/滤镜管线使用 GLStateManager 缓存 GL 状态，
                                // 并且可能绑定非零 VAO/VBO。如果显示层在非零 VAO 状态下调用
                                // glVertexAttribPointer(client-side FloatBuffer)，会触发
                                // GL_INVALID_OPERATION，后续绘制全部失败 → 清黑 → 黑屏。
                                //
                                // 每一帧强制归零以下状态，确保显示层在全量已知状态下绘制。
                                GLES30.glBindVertexArray(0)
                                GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
                                GLES20.glBindBuffer(GLES20.GL_ELEMENT_ARRAY_BUFFER, 0)
                                GLES20.glDisable(GLES20.GL_DEPTH_TEST)
                                GLES20.glDisable(GLES20.GL_BLEND)
                                GLES20.glDisable(GLES20.GL_CULL_FACE)
                                GLES20.glDisable(GLES20.GL_SCISSOR_TEST)
                                // 每帧恢复 GLSurfaceView 真实 viewport。
                                // native 管线 FBO 渲染时会改成引擎尺寸，不恢复则
                                // 显示层只画到引擎 viewport 区域 → 其余黑屏。
                                if (surfaceWidth > 0 && surfaceHeight > 0) {
                                    GLES20.glViewport(0, 0, surfaceWidth, surfaceHeight)
                                }
                                // ──────────────────────────────────────────────────────────

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

                                // ── 绘制后将显示层修改过的 GL 状态归零 ────────────────────
                                // 虽然 native processFrame() 每帧会 invalidate GLStateManager
                                // 缓存，但主动将 program 和 texture 绑定归零可以减少状态泄漏
                                // 风险，让下一次 native 帧处理从一个确定的干净状态开始。
                                GLES20.glUseProgram(0)
                                GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
                                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
                                // ──────────────────────────────────────────────────────────

                                // Drain GL errors left by the display shader rendering.
                                // All GL calls above bypass GLStateManager and may leave
                                // GL_INVALID_OPERATION in the error queue, which would poison
                                // the next frame's updateTexImage.
                                var err = GLES20.glGetError()
                                while (err != GLES20.GL_NO_ERROR) {
                                    android.util.Log.w("FilterCameraPreview",
                                        "onDrawFrame GL error: 0x${Integer.toHexString(err)}")
                                    err = GLES20.glGetError()
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
