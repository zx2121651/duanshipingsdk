# ShortVideo SDK 架构说明文档

> 文档目标：说明当前项目在 **核心渲染引擎、Android/iOS 平台适配、Timeline 编辑子系统、错误处理与性能策略** 上的整体设计与关键流程。

---

## 1. 项目概览

本项目采用「**C++ Core + 平台桥接层**」架构：

- `core/`：跨平台滤镜引擎、帧缓冲池、OpenGL 能力探测、Timeline/Compositor。
- `android/`：CameraX 采集、Kotlin Facade、JNI Bridge、MediaCodec 编码与封装。
- `ios/`：Swift Facade + Objective-C++ Wrapper，连接 C++ 核心与 AVFoundation。

该模式实现了平台能力和算法核心的分离：滤镜逻辑集中在 `core`，平台层只负责采集/展示/编码与线程调度。

---

## 2. 分层架构

### 2.1 Core 层（C++）

#### 2.1.1 FilterEngine（滤镜流水线总控）

职责：

1. 初始化（绑定渲染线程、能力嗅探、初始化滤镜）。
2. 逐滤镜处理帧（Texture -> FBO -> Texture）。
3. 参数广播到滤镜链。
4. 管理滤镜生命周期和 FBO 资源。

关键点：

- 通过 `ThreadCheck` 限制 `processFrame` 必须在绑定线程执行。
- 根据 `GLContextManager` 探测结果，动态选择中间 FBO 精度（`FP16 / RGB565 / RGBA8888`）。
- 使用 `FrameBufferPool` 做复用，避免频繁分配和销毁 FBO。

#### 2.1.2 Filter 抽象与具体实现

- `Filter` 是所有滤镜的基类，约定 `initialize / processFrame / setParameter / release`。
- 子类实现 `onDraw` 并提供 shader。
- 现有滤镜包括：
  - `OES2RGBFilter`
  - `BrightnessFilter`
  - `GaussianBlurFilter`
  - `LookupFilter`
  - `BilateralFilter`
  - `CinematicLookupFilter`
  - `ComputeBlurFilter`（Android 条件编译）

#### 2.1.3 GLContextManager（硬件能力探测）

在 GL Context 就绪后执行 capability sniffing：

- FP16 Render Target 支持
- ASTC 支持
- Compute Shader 支持（并对 `GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS` 做阈值校验）

这样可实现“可用即启用、不可用即降级”的稳态策略，避免驱动异常导致崩溃。

#### 2.1.4 FrameBuffer & FrameBufferPool

- `FrameBuffer` 统一封装 FBO + texture 资源。
- `FrameBufferPool` 按 `(width, height, precision)` key 复用对象。
- 池对象通过自定义 deleter 自动归还，降低泄漏风险。

---

### 2.2 Android 层

#### 2.2.1 VideoFilterManager（Android Facade）

职责：

- 封装 `RenderEngine`（JNI 代理）并对外提供统一 API。
- 通过 `StateFlow` 暴露引擎状态（`STOPPED/INITIALIZING/RUNNING/DEGRADED/ERROR`）。
- 通过 `SharedFlow<Result<Int>>` 输出处理后的纹理 ID。
- 通过 `glThreadDispatcher` 把 GL 操作收敛到渲染线程。

#### 2.2.2 RenderEngine（Kotlin JNI 包装）

职责：

- 管理 native handle。
- 承担 JNI 调用入口（`nativeInit/nativeProcessFrame/nativeAddFilter/...`）。
- 提供录制 surface 绑定、参数更新、音频控制 API。

#### 2.2.3 NativeBridge（C++ JNI 实现）

职责：

- 创建 `EngineWrapper(FilterEngine + OboeAudioEngine)`。
- 默认插入 `OES2RGBFilter`。
- 每帧接收 OES texture + transform matrix，调用 `FilterEngine::processFrame`。
- 可将输出纹理额外渲染到 MediaCodec 的输入 Surface（录制路径）。

#### 2.2.4 MainActivity（业务编排）

- CameraX `Preview` 作为采集入口。
- 从 `VideoFilterManager` 异步获取输入 surface 并喂给 CameraX。
- 录制时创建 `VideoEncoder`，绑定编码输入 Surface 到 native 渲染链。

#### 2.2.5 VideoEncoder（Android 编码）

- 视频：`MediaCodec` AVC + Surface 输入。
- 音频：AAC 编码；PCM 来源为 native Oboe 采集。
- 采用“双轨格式就绪后启动 muxer”策略，避免早期数据丢失。

---

### 2.3 iOS 层

#### 2.3.1 Swift `VideoFilterManager`（actor）

- 用 `actor` 保证状态和调用串行安全。
- 使用 `AsyncStream` 向上层输出处理后帧。

#### 2.3.2 Objective-C++ `FilterEngineWrapper`

- 初始化 `CVOpenGLESTextureCache`。
- 输入 `CVPixelBuffer` 映射为 GL 纹理。
- 调用 C++ `FilterEngine` 渲染。
- 输出阶段通过 `CVPixelBufferPool` + FBO blit 回写，尽量降低拷贝开销。

#### 2.3.3 iOS `VideoEncoder`

- 基于 `AVAssetWriter` 管理音视频写入。
- 支持实时 append 视频像素缓冲与音频 sample buffer。

---

## 3. 关键数据流

### 3.1 Android 实时预览链

```text
CameraX Preview (Producer)
    -> SurfaceTexture (OES)
    -> RenderEngine.nativeProcessFrame
    -> FilterEngine(Filter Chain)
    -> GLSurfaceView onDrawFrame (Consumer)
```

### 3.2 Android 录制链

```text
FilterEngine Output Texture
    -> NativeBridge.renderToRecordingSurface
    -> MediaCodec Input Surface
    -> MediaMuxer (AAC + AVC)
```

### 3.3 iOS 处理链

```text
AVCaptureVideoDataOutput PixelBuffer
    -> FilterEngineWrapper (CVOpenGLESTextureCache)
    -> C++ FilterEngine
    -> Out PixelBuffer (via blit)
    -> AsyncStream Consumer / AVAssetWriter
```

### 3.4 Timeline/NLE 合成链

```text
Timeline (tracks/clips)
    -> DecoderPool 获取当前时间点帧
    -> Compositor 做叠加/转场
    -> FilterEngine 后处理
    -> Exporter/Output FBO
```

---

## 4. Timeline 子系统设计

`Timeline` 管理工程级参数（输出分辨率、fps）和轨道集合（按 z-index）。

- `getActiveVideoClipsAtTime(t)`：返回当前时刻参与视频合成的 clip 集。
- `getActiveAudioClipsAtTime(t)`：返回参与混音的 clip 集。

`Compositor::renderFrameAtTime` 负责：

1. 拉取活跃 clip。
2. 从 `DecoderPool` 获取对应时间帧。
3. 做 copy / blend / transition（如 crossfade、wipe）。
4. 走 `FilterEngine` 做最终滤镜处理。
5. 输出到目标 FBO。

Android 通过 `TimelineBridge.cpp` 暴露基础 JNI（创建时间线、添加轨道、添加素材）。

---

## 5. 错误处理模型

### 5.1 Native 统一错误码

`core/include/GLTypes.h` 统一定义错误码：

- 初始化错误：`-1001 ~ -1003`
- 渲染错误：`-2001 ~ -2003`
- Timeline 错误：`-3001 ~ -3003`

并提供 `Result` 作为 C++ 返回封装。

### 5.2 Android 映射

`VideoSdkError.kt` 对 native 错误码映射为 Kotlin 异常类型，并提供 `toResult` 便捷方法。

---

## 6. 并发与线程模型

### 6.1 C++ Core

- `FilterEngine` 初始化时绑定线程。
- `processFrame` 非绑定线程调用将触发线程检查失败并拒绝处理。

### 6.2 Android

- `VideoFilterManager` 通过 `glThreadDispatcher` 强制 GL 调用在指定线程执行。
- 编码与音频读取由独立协程作用域处理。

### 6.3 iOS

- `VideoFilterManager` 使用 `actor` 串行化状态变更与 API 调用。

---

## 7. 性能与稳定性策略

1. **FBO 复用池化**：减少分配抖动。
2. **能力探测与降级**：避免盲开高特性导致崩溃。
3. **背压保护**：Android 输出流使用 `DROP_OLDEST` 防止积压。
4. **状态暴露**：`RUNNING/DEGRADED/ERROR` 便于上层策略切换。

---

## 8. 已识别的架构风险

1. **Shader 来源不统一**：既有 `assets/shaders`，也有 C++ 内嵌 shader 字符串，建议统一来源。
2. **错误契约仍有不一致**：部分 JNI 失败路径采用默认返回值，可能掩盖故障。
3. **Timeline 与主实时链整合深度有限**：目前更偏“并行模块”，可继续打通。
4. **iOS 编码路径与 VideoToolbox 目标存在差异**：当前主要使用 AVAssetWriter。

---

## 9. 建议的下一步演进

1. 统一 shader 管理（Asset 化 + 热更新/版本管理）。
2. 收紧 JNI 错误返回语义（避免静默 fallback）。
3. 将 Timeline 与预览/导出流程统一成标准化 Pipeline Node。
4. 增加可观测性：帧耗时分位数、drop frame、编码队列滞留等。
5. 补充平台差异文档（Android/iOS 功能矩阵与降级行为）。

---

## 10. 术语

- **OES Texture**：Camera/SurfaceTexture 常用外部纹理类型。
- **FBO**：Frame Buffer Object，离屏渲染目标。
- **NLE**：Non-Linear Editing，非线性编辑。
- **Backpressure**：消费速度低于生产速度时的系统压力。
- **Degraded Mode**：能力不足或故障时的降级运行状态。
