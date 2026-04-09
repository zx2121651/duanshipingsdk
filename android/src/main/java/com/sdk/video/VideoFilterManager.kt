package com.sdk.video

import android.graphics.SurfaceTexture
import android.view.Surface
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.*

// 定义支持的视频滤镜类型枚举
enum class VideoFilterType {
    BRIGHTNESS, GAUSSIAN_BLUR, LOOKUP, BILATERAL, CINEMATIC_LOOKUP, COMPUTE_BLUR
}

// 引擎运行状态，供上层 UI 监听，以便在发生错误或退化渲染时给出提示
enum class FilterEngineState {
    STOPPED, INITIALIZING, RUNNING, DEGRADED, ERROR
}

/**
 * 跨平台实时视频滤镜的 Android 端门面类 (Facade)。
 * 核心设计：
 * 1. 使用 Kotlin 协程和专属的单线程调度器 (GLRenderThread) 来保障 OpenGL 上下文的线程安全。
 * 2. 使用 SharedFlow (共享数据流) 作为生产者-消费者模型，向外界抛出处理后的纹理 ID，解耦底层渲染与上层 UI。
 */
@OptIn(InternalApi::class)
class VideoFilterManager(private val context: android.content.Context,
    private val width: Int,
    private val height: Int,
    // 允许外部传入协程作用域，默认创建一个后台任务域
    val scope: CoroutineScope = CoroutineScope(Dispatchers.Default + SupervisorJob())
) {
    private var videoEncoder: VideoEncoder? = null

    // 实例化底层的 C++ JNI 包装类
    private val renderEngine = RenderEngine(width, height)

    // 输入的 Surface，将提供给相机硬件 (如 CameraX) 捕获画面流
    private var inputSurface: Surface? = null

    // 重点：为所有的 GL 操作分配一个专属的单线程。
    // 在 OpenGL 中，所有的 Context 和 Texture ID 都绑定在特定的线程上。
    // 必须确保所有对 renderEngine 的调用都在这个单线程中进行。

    // GL 线程分发器：为了消除对具体 UI 控件 (GLSurfaceView) 的直接依赖，
    // 我们强制外部消费者在创建引擎时提供一个回调，用于将任务派发给 EGL 绑定的渲染线程。
    // 这解决了之前 "依靠约定保证单线程" 带来的巨大维护隐患。
    var glThreadDispatcher: ((Runnable) -> Unit)? = null

    // 将所有的配置更新请求统一收敛并安全分发给 GL 线程
    private fun runOnGLThread(action: () -> Unit): Result<Unit> {
        val dispatcher = glThreadDispatcher
        if (dispatcher != null) {
            dispatcher.invoke(Runnable { action() })
            return Result.success(Unit)
        } else {
            // Fail-fast: 拒绝在未知线程执行可能导致崩溃的 GL 调用
            return Result.failure(IllegalStateException("GL thread dispatcher not bound. Cannot execute GL action."))
        }
    }

    // StateFlow 暴露引擎状态，外部 UI (如 Compose) 可以直接 collectAsState() 监听变化
    private val _engineState = MutableStateFlow(FilterEngineState.STOPPED)
    val engineState: StateFlow<FilterEngineState> = _engineState.asStateFlow()

    /**
     * 核心输出流：向外发射每次渲染完成后的纹理 ID。
     * 为什么使用 BufferOverflow.DROP_OLDEST？
     * 当底层 GPU 渲染帧率过高（比如 60fps），而上层 UI 消费能力不足时，
     * 直接丢弃旧帧（即降帧），避免堆积导致内存溢出 (OOM)。这就是典型的防背压 (Backpressure) 降级策略。
     */
    private val _processedFrames = MutableSharedFlow<Result<Int>>(
        extraBufferCapacity = 1,
        onBufferOverflow = BufferOverflow.DROP_OLDEST
    )
    val processedFrames: SharedFlow<Result<Int>> = _processedFrames.asSharedFlow()

    private val _performanceMetrics = MutableStateFlow<RenderEngine.PerformanceMetrics?>(null)
    val performanceMetrics: StateFlow<RenderEngine.PerformanceMetrics?> = _performanceMetrics.asStateFlow()

    // 初始化引擎，必须切换到专属的 GL 线程
    fun initialize(): Result<Unit> {
        try {
            _engineState.value = FilterEngineState.INITIALIZING
            val res = renderEngine.init(context.assets)
            if (res != 0) return Result.failure(VideoSdkError.fromNativeCode(res))

            // 设置底层每处理完一帧的回调监听
            renderEngine.onFrameProcessedListener = { outputTexId ->
                if (outputTexId >= 0) {
                    // 成功渲染，发送最新的纹理 ID 给 UI 层
                    _processedFrames.tryEmit(Result.success(outputTexId))
                } else {
                    // 异常降级：如果底层 C++ 渲染失败返回非法 ID，则抛出异常
                    _processedFrames.tryEmit(Result.failure(RuntimeException("GL Engine Render Failed")))
                    _engineState.value = FilterEngineState.DEGRADED // 状态变为降级
                }
            }

            // 获取底层的 OES 纹理载体，包装为 Android 的 Surface，供 CameraX 使用
            val st = renderEngine.getSurfaceTexture()
            if (st != null) {
                inputSurface = Surface(st)
                _engineState.value = FilterEngineState.RUNNING
                return Result.success(Unit)
            } else {
                return Result.failure(Exception("Failed to create input surface"))
            }
        } catch (e: Exception) {
            _engineState.value = FilterEngineState.ERROR
            _processedFrames.tryEmit(Result.failure(e))
            return Result.failure(e)
        }
    }

    suspend fun awaitInputSurface(): Surface {
        // 彻底告别脆弱的 delay(500)，使用协程 Flow 挂起等待引擎真正初始化完毕
        engineState.first { it == FilterEngineState.RUNNING }
        return inputSurface ?: throw IllegalStateException("Surface is null even though engine is RUNNING")
    }

    // 提供给相机的输入层
    fun getInputSurface(): Surface? {
        return inputSurface
    }

    // 动态添加滤镜
    fun addFilter(type: VideoFilterType): Result<Unit> {
        return runOnGLThread {
            val typeInt = when (type) {
                VideoFilterType.BRIGHTNESS -> RenderEngine.FILTER_TYPE_BRIGHTNESS
                VideoFilterType.GAUSSIAN_BLUR -> RenderEngine.FILTER_TYPE_GAUSSIAN_BLUR
                VideoFilterType.LOOKUP -> RenderEngine.FILTER_TYPE_LOOKUP
                VideoFilterType.BILATERAL -> RenderEngine.FILTER_TYPE_BILATERAL
                VideoFilterType.CINEMATIC_LOOKUP -> 4 // 刚才在 C++ 里新增的电影级 LUT
                VideoFilterType.COMPUTE_BLUR -> 5 // OpenGL ES 3.1 计算着色器测试
            }
            renderEngine.addFilter(typeInt)
        }
    }

    // 清空滤镜管线
    fun removeAllFilters(): Result<Unit> {
        return runOnGLThread { renderEngine.removeAllFilters() }
    }

    // 更新滤镜参数 (Float)
    fun updateParameter(key: String, value: Float): Result<Unit> {
        return runOnGLThread { renderEngine.updateParameterFloat(key, value) }
    }

    // 更新滤镜参数 (Int)
    fun updateParameter(key: String, value: Int): Result<Unit> {
        return runOnGLThread { renderEngine.updateParameterInt(key, value) }
    }


    // --- 音视频录制代理方法 ---

    fun startVideoRecording(config: VideoExportConfig): Result<Unit> {
        return try {
            val encoder = VideoEncoder(this, config)
            val surface = encoder.startRecording()
            if (surface != null) {
                videoEncoder = encoder
                renderEngine.startRecording(surface)
            } else {
                Result.failure(VideoSdkError.InvalidOperation("Failed to start MediaCodec encoder"))
            }
        } catch (e: Exception) {
            Result.failure(VideoSdkError.InvalidOperation(e.message ?: "Encoder exception"))
        }
    }

    fun stopVideoRecording(isFallback: Boolean = false) {
        renderEngine.stopRecording()
        videoEncoder?.stopRecording(isFallback)
        videoEncoder = null
    }

    // Oboe 音频控制不需要强制在 GL 线程，但为了统一管理也可以放进来
    fun startAudioRecord(sampleRate: Int): Result<Unit> {
        return renderEngine.startAudioRecord(sampleRate)
    }

    fun stopAudioRecord(): Result<Unit> {
        return renderEngine.stopAudioRecord()
    }

    fun readAudioPCM(buffer: ByteArray, length: Int): Int {
        return renderEngine.readAudioPCM(buffer, length)
    }

    // 暴露性能监控监听器供外部（或者修改为 StateFlow 抛出）
    // 手动触发一帧处理
    fun processFrame() {
        renderEngine.getSurfaceTexture()?.let { st ->
            renderEngine.onFrameAvailable(st)
        }
    }

    // 释放所有的硬件及线程资源

    fun updateShaderSource(name: String, source: String): Result<Unit> {
        return runOnGLThread {
            val res = renderEngine.updateShaderSource(name, source)
            if (res == 0) Result.success(Unit) else Result.failure(VideoSdkError.fromNativeCode(res))
        }
    }
    fun release() {
        inputSurface?.release()
        inputSurface = null
        try { renderEngine.release() } catch (e: Exception) { Log.e("VideoFilterManager", "Error releasing RenderEngine", e) }
        _engineState.value = FilterEngineState.STOPPED
        scope.cancel() // 取消协程域中的所有任务
    }
}
