package com.sdk.video

import android.graphics.SurfaceTexture
import android.view.Surface
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow

enum class VideoFilterType {
    BRIGHTNESS, GAUSSIAN_BLUR, LOOKUP, BILATERAL
}

class VideoFilterManager(
    private val width: Int,
    private val height: Int,
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.Default + SupervisorJob())
) {
    private val renderEngine = RenderEngine(width, height)
    private var inputSurface: Surface? = null

    // Channel to queue frame processing requests
    private val frameChannel = Channel<Unit>(Channel.CONFLATED)

    // Flow to emit processed texture IDs back to the consumer
    private val _processedFrames = MutableSharedFlow<Int>(extraBufferCapacity = 1)
    val processedFrames: SharedFlow<Int> = _processedFrames.asSharedFlow()

    // A single threaded dispatcher for all GL operations
    @OptIn(DelicateCoroutinesApi::class)
    private val glDispatcher = newSingleThreadContext("GLRenderThread")

    suspend fun initialize() = withContext(glDispatcher) {
        renderEngine.init()

        // Setup listener on RenderEngine to trigger coroutine processing
        renderEngine.onFrameProcessedListener = { outputTexId ->
            coroutineScope.launch {
                _processedFrames.emit(outputTexId)
            }
        }

        // Wrap SurfaceTexture in a Surface for Producer (e.g., CameraX)
        val st = renderEngine.getSurfaceTexture()
        if (st != null) {
            inputSurface = Surface(st)
        }
    }

    suspend fun getInputSurface(): Surface? = withContext(glDispatcher) {
        inputSurface
    }

    suspend fun addFilter(type: VideoFilterType) = withContext(glDispatcher) {
        val typeInt = when (type) {
            VideoFilterType.BRIGHTNESS -> RenderEngine.FILTER_TYPE_BRIGHTNESS
            VideoFilterType.GAUSSIAN_BLUR -> RenderEngine.FILTER_TYPE_GAUSSIAN_BLUR
            VideoFilterType.LOOKUP -> RenderEngine.FILTER_TYPE_LOOKUP
            VideoFilterType.BILATERAL -> RenderEngine.FILTER_TYPE_BILATERAL
        }
        renderEngine.addFilter(typeInt)
    }

    suspend fun removeAllFilters() = withContext(glDispatcher) {
        renderEngine.removeAllFilters()
    }

    suspend fun updateParameter(key: String, value: Float) = withContext(glDispatcher) {
        renderEngine.updateParameterFloat(key, value)
    }

    suspend fun updateParameter(key: String, value: Int) = withContext(glDispatcher) {
        renderEngine.updateParameterInt(key, value)
    }

    // Call this if not using SurfaceTexture automatic updates, to manually trigger process
    suspend fun processFrame() = withContext(glDispatcher) {
        renderEngine.getSurfaceTexture()?.let { st ->
            // Manually trigger the listener logic if needed, usually st.setOnFrameAvailableListener does this
            renderEngine.onFrameAvailable(st)
        }
    }

    suspend fun release() {
        withContext(glDispatcher) {
            inputSurface?.release()
            inputSurface = null
            renderEngine.release()
        }
        coroutineScope.cancel()
        glDispatcher.close()
    }
}
