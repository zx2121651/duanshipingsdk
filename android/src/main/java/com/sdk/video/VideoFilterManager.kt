package com.sdk.video

import android.graphics.SurfaceTexture
import android.view.Surface
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
class VideoFilterManager(
    private val width: Int,
    private val height: Int,
    // 允许外部传入协程作用域，默认创建一个后台任务域
    val scope: CoroutineScope = CoroutineScope(Dispatchers.Default + SupervisorJob())
) {
    // 实例化底层的 C++ JNI 包装类
    private val renderEngine = RenderEngine(width, height)

    // 输入的 Surface，将提供给相机硬件 (如 CameraX) 捕获画面流
    private var inputSurface: Surface? = null

    // 重点：为所有的 GL 操作分配一个专属的单线程。
    // 在 OpenGL 中，所有的 Context 和 Texture ID 都绑定在特定的线程上。
    // 必须确保所有对 renderEngine 的调用都在这个单线程中进行。
    @OptIn(DelicateCoroutinesApi::class)
    private val glDispatcher = newSingleThreadContext("GLRenderThread")

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

    // 初始化引擎，必须切换到专属的 GL 线程
    suspend fun initialize() = withContext(glDispatcher) {
        try {
            _engineState.value = FilterEngineState.INITIALIZING
            renderEngine.init() // 调用底层的 Native 初始化

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
            } else {
                throw IllegalStateException("Failed to create input surface")
            }
        } catch (e: Exception) {
            _engineState.value = FilterEngineState.ERROR
            _processedFrames.tryEmit(Result.failure(e))
        }
    }

    // 提供给相机的输入层
    suspend fun getInputSurface(): Surface? = withContext(glDispatcher) {
        inputSurface
    }

    // 动态添加滤镜
    suspend fun addFilter(type: VideoFilterType) = withContext(glDispatcher) {
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

    // 清空滤镜管线
    suspend fun removeAllFilters() = withContext(glDispatcher) {
        renderEngine.removeAllFilters()
    }

    // 更新滤镜参数 (Float)
    suspend fun updateParameter(key: String, value: Float) = withContext(glDispatcher) {
        renderEngine.updateParameterFloat(key, value)
    }

    // 更新滤镜参数 (Int)
    suspend fun updateParameter(key: String, value: Int) = withContext(glDispatcher) {
        renderEngine.updateParameterInt(key, value)
    }


    // --- 音视频录制代理方法 ---

    suspend fun startVideoRecording(surface: Surface) = withContext(glDispatcher) {
        renderEngine.startRecording(surface)
    }

    suspend fun stopVideoRecording() = withContext(glDispatcher) {
        renderEngine.stopRecording()
    }

    // Oboe 音频控制不需要强制在 GL 线程，但为了统一管理也可以放进来
    fun startAudioRecord(sampleRate: Int) {
        renderEngine.startAudioRecord(sampleRate)
    }

    fun stopAudioRecord() {
        renderEngine.stopAudioRecord()
    }

    fun readAudioPCM(buffer: ByteArray, length: Int): Int {
        return renderEngine.readAudioPCM(buffer, length)
    }

    // 暴露性能监控监听器供外部（或者修改为 StateFlow 抛出）
    fun setOnPerformanceUpdateListener(listener: (Long) -> Unit) {
        renderEngine.onPerformanceUpdateListener = listener
    }

    // 手动触发一帧处理
    suspend fun processFrame() = withContext(glDispatcher) {
        renderEngine.getSurfaceTexture()?.let { st ->
            renderEngine.onFrameAvailable(st)
        }
    }

    // 释放所有的硬件及线程资源
    suspend fun release() {
        withContext(glDispatcher) {
            inputSurface?.release()
            inputSurface = null
            try { renderEngine.release() } catch (e: Exception) { /* 忽略释放错误 */ }
        }
        _engineState.value = FilterEngineState.STOPPED
        scope.cancel() // 取消协程域中的所有任务
        glDispatcher.close() // 关闭专属 GL 线程
    }
}
