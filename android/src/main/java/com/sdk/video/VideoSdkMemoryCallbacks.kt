package com.sdk.video

import android.content.ComponentCallbacks2
import android.content.res.Configuration

/**
 * P2-14: Android 内存压力回调适配器。
 *
 * 将系统内存压力信号自动转发给 [RenderEngine.onTrimMemory]，
 * 无需手动在 Application/Activity 中实现 ComponentCallbacks2。
 *
 * 用法：
 * ```kotlin
 * // Application.onCreate() 中注册：
 * application.registerComponentCallbacks(VideoSdkMemoryCallbacks(renderEngine))
 * ```
 */
class VideoSdkMemoryCallbacks(private val renderEngine: RenderEngine) : ComponentCallbacks2 {

    override fun onTrimMemory(level: Int) {
        renderEngine.onTrimMemory(level)
    }

    override fun onLowMemory() {
        // onLowMemory() 等价于 TRIM_MEMORY_COMPLETE（level = 80）
        renderEngine.onTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        // 无需响应配置变化
    }
}
