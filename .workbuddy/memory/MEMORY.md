# ShortVideoSDK 项目长期记忆

## 关键架构约定
- Android SDK 模块位于 `android/android/`
- C++ 核心渲染引擎位于 `sdk-core/core/`
- Kotlin 层通过 JNI 调用 C++ FilterEngine
- GL 线程通过 `glThreadDispatcher` 单线程调度，所有 GL 操作必须在 GL 线程执行

## 已知问题与修复经验
- **GL error 0x502 泄漏**: C++ `FilterEngine::processFrame` 中 `m_graph->execute()` 会产生 GL error，需在 execute 后和 Kotlin `nativeProcessFrame` 后都做 drain
- **ProGuard/Compose**: R8 需添加 `androidx.compose.runtime.snapshots.**` keep 规则
- **SurfaceTexture 线程安全**: `setOnFrameAvailableListener` 必须通过 `glThreadDispatcher` 路由到 GL 线程

## 依赖版本
- CameraX + GLES 3.2 渲染管线
- Oboe 音频引擎
