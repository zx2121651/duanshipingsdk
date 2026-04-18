# ShortVideo SDK 架构说明文档

> 文档目标：说明当前项目在 **核心渲染引擎、Android/iOS 平台适配、Timeline 编辑子系统、错误处理与性能策略** 上的整体设计与关键流程。

---

## 0. 快速上手 (Getting Started)

对于新开发者或 Agent，请首先查阅 [BUILD.md](./BUILD.md) 以获取完整的**本地构建前置条件说明**。

- **Android**: 确保安装 JDK 17 和 Android SDK/NDK。
- **iOS**: 需要 macOS 和 Xcode 环境。
- **Core C++**: 依赖 CMake 3.10.2+。

---

## 1. 项目概览与目录结构

本项目采用「**C++ Core + 平台桥接层**」架构，实现了算法核心与平台能力的解耦。

### 1.1 核心目录与模块定位

- **`core/` (C++ Core)**: 跨平台引擎核心。包含渲染管线 (PipelineGraph)、滤镜引擎 (FilterEngine)、帧缓冲池 (FrameBufferPool)、OpenGL 能力探测、Timeline 编辑子系统。
- **`android/` (Android SDK Library)**: Android 端的 SDK 库实现。包含 CameraX 采集封装、Kotlin Facade、JNI Bridge、MediaCodec 硬件编解码。该目录输出 `aar` 供集成使用。
- **`sample-android/` (Android Sample Host)**: Android 示例宿主 App。用于演示 SDK 的集成方式（`:android` 模块的消费者），并作为日常开发的集成验证环境。
- **`ios/` (iOS SDK Library)**: iOS 端的 SDK 实现。基于 Swift Facade 和 Objective-C++ Wrapper，连接 C++ 核心与 AVFoundation。
- **`tests/`**: 核心引擎的 C++ 单元测试与回归测试用例，确保跨平台逻辑的稳定性。
- **`mock_gl/`**: 为 headless 环境（如 CI 容器）提供的 Mock OpenGL 接口，支持在无 GPU 环境下编译核心代码。
- **Gradle Wrapper (`gradle/`, `gradlew`, `gradlew.bat`)**: 统一的 Android 构建环境版本管理，确保本地与 CI 构建的一致性。

### 1.2 设计哲学

该架构将渲染与流水线逻辑集中在 `core`，而平台层负责：
1. **采集与展示**: 处理系统级 Camera/Display 回调。
2. **硬件编解码**: 调度 MediaCodec (Android) 或 AVFoundation (iOS)。
3. **线程调度**: 管理平台特定的渲染线程与后台任务。

---

## 2. 分层架构

### 2.1 Core 层（C++）

#### 2.1.1 PipelineGraph (渲染管线)

核心渲染架构已从线性滤镜链演进为**基于拉模式（Pull-model）的 PipelineGraph**：

- **PipelineNode**: 所有处理节点的基类，定义了 `pullFrame(timestamp)` 接口。
- **PipelineGraph**: 管理节点拓扑、循环检测、拓扑排序及执行驱动。
  - `compile()`: 执行循环检测与拓扑排序，初始化所有节点。
  - `execute(timestamp)`: 驱动所有 **Sink 节点**调用 `pullFrame`，触发整条管线的递归拉取。
- **节点类型**:
  - `CameraInputNode`: 作为 Source，接收平台层推送的 OES/RGB 纹理。
  - `TimelineNode`: 将 Timeline/Compositor 接入管线作为输入源。
    - **FBO 注入**: 在 `FilterEngine` 构建管线时，会将全局 `FrameBufferPool` 注入 `TimelineNode`。
    - **尺寸安全**: 在 `pullFrame` 时强制对输出尺寸进行 `max(1, width/height)` 钳位，防止 OpenGL 报错。
  - `FilterNode`: 包装 `Filter` 对象，负责具体的纹理处理。
  - `OutputNode`: 作为 Sink，收集最终渲染结果。

#### 2.1.2 FilterEngine（管线总控）

职责：
1. **初始化**: 绑定渲染线程、触发 `GLContextManager` 能力嗅探。
2. **动态构建**: 根据业务场景（预览/时间线编辑）动态组装 `PipelineGraph`。
   - **事务性重构 (rebuildGraph)**: 采用事务机制，仅在管线成功 `compile()` 后才更新引擎状态（`m_graph`, `m_outputNode` 等），确保失败时不破坏当前运行状态。
3. **参数广播**: 遍历管线节点，将全局参数（如滤镜强度）下发给对应的 `FilterNode`。

#### 2.1.3 RHI (Rendering Hardware Interface)

为了支持未来向 Vulkan/Metal 迁移，项目引入了 RHI 抽象层：
- **`IRenderDevice`**: 定义了基本的渲染设备接口（如资源创建、管线状态设置）。
- **`GLRenderDevice`**: 当前主力的 OpenGL ES 实现。
- `FilterEngine` 在初始化时实例化具体的 RHI 后端，并将其透传给各滤镜节点。

#### 2.1.4 GLContextManager（硬件能力探测）

在 GL Context 就绪后执行 capability sniffing：
- FP16 Render Target 支持。
- Compute Shader 支持（校验 `GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS`）。
- 根据探测结果自动配置 `FilterNode` 的中间 FBO 精度（`FP16 / RGB565 / RGBA8888`）。

#### 2.1.5 FrameBufferPool

- 实现**全局显存预算管理**（默认 256MB）。
- 采用 **LRU 淘汰策略**，按 `(width, height, precision)` 复用 FBO。
- `VideoFrame` 持有 `FrameBuffer` 的智能指针，确保资源在帧生命周期结束后自动归还。

---

### 2.2 Android 层

#### 2.2.1 VideoFilterManager

职责：
- 封装 `RenderEngine` (JNI) 并提供协程友好的 API。
- **线程模型**:
  - 使用 `glThreadDispatcher` 确保所有 GL 操作收敛到单一渲染线程。
  - 通过 `runOnGLThread` 挂起协程并同步等待 GL 任务返回结果。
- **背压控制与 frame delivery**:
  - `processedFrames`: 使用 `SharedFlow` 向外发射处理后的纹理 ID。
  - 采用 `onBufferOverflow = BufferOverflow.SUSPEND` 并配合 `tryEmit` 探测丢帧，实现生产与消费的平衡。

#### 2.2.2 NativeBridge (JNI)

- 遵循 `Java_com_sdk_video_` 命名规范。
- 负责 Native `FilterEngine` 的生命周期管理。
- 处理 JNI Local Reference 释放，防止 NDK 内存泄漏。

#### 2.2.3 VideoEncoder

- `MediaCodec` 硬编码，支持 Surface 输入。
- 音频：PCM 来源于 Oboe 引擎，与视频进行时间戳同步。

---

### 2.3 iOS 层

#### 2.3.1 VideoFilterManager (Swift Actor)

- 利用 Swift `actor` 机制保证状态变更与 API 调用在并发环境下的安全性。
- 串行化对 `EAGLContext` 的访问，防止多线程竞争导致的崩溃。

#### 2.3.2 FilterEngineWrapper (ObjC++)

- 桥接 Swift 与 C++。
- 使用 `CVOpenGLESTextureCache` 实现 `CVPixelBuffer` 与 GL 纹理的零拷贝/低开销映射。
- 特别注意：iOS 端映射 BGRA 格式需显式包含 `<OpenGLES/ES3/glext.h>` 并使用 `GL_BGRA_EXT`。

---

## 3. 关键数据流

### 3.1 渲染管线执行流 (Pull Model)

```text
FilterEngine::processFrame(t) 或 PipelineGraph::execute(t)
    -> OutputNode::pullFrame(t)
        -> FilterNode::pullFrame(t)
            -> upstreamNode::pullFrame(t) (Recursively until source)
                -> CameraInputNode / TimelineNode (Provide Source Frame)
            -> Filter::processFrame (Draw to FBO using FrameBufferPool)
        -> Final Texture result collected by OutputNode
```

### 3.2 Android 导出/录制链

```text
Compositor::renderFrameAtTime(t, fbo)
    -> PipelineGraph Output (if filters active)
    -> eglSwapBuffers to MediaCodec Input Surface
    -> AMediaCodec (Drain Output Buffers)
    -> AMediaMuxer (Write Sample Data)
```

---

## 4. Timeline 子系统与导出

### 4.1 Timeline 架构

- `Timeline`: 管理多个轨道 (`Track`) 和剪辑 (`Clip`)。
- `Compositor`: 负责多轨道视频帧的叠加、混合与转场。
- `DecoderPool`: 管理多个 `VideoDecoder` 实例，支持 **硬件解码 -> 软件解码 (fallback)** 切换，最大并发限制为 4。

### 4.2 离线导出 (TimelineExporter)

- 支持**分片导出 (Chunked Export)**：可按关键帧间隔将输出切割为多个 MP4 片段，支持“边导边传”业务逻辑。
- 导出过程在独立的后台 GL 线程执行，不阻塞 UI 或预览。

---

## 5. 错误处理模型

### 5.1 ResultPayload 机制

C++ 层统一返回 `ResultPayload<T>`，包含：
- `isOk()`: 执行是否成功。
- `getErrorCode()`: 详细错误码。
- `getValue()`: 成功时的有效负载。

### 5.2 错误码范围

- **初始化错误 (-1001 ~ -1999)**: 如上下文创建失败、Shader 编译失败。
- **渲染错误 (-2001 ~ -2999)**: 如 FBO 分配失败、不支持 Compute Shader。
- **Timeline 错误 (-3001 ~ -3999)**: 如剪辑未找到、解码器崩溃。
  - **解码器硬核降级**: 如 `ERR_DECODER_SEEK_FAILED` (-3007) 表示硬解无法精确定位，将触发 `DEGRADED` 状态，由 SDK 层逻辑决定是否切换软解或跳帧。

---

## 6. 并发与线程模型

- **C++ Core**:
  - 核心逻辑基于单一渲染线程。
  - **`ThreadCheck`**: 每个 `FilterEngine` 实例会绑定初始化时的线程 ID，所有关键调用（如 `processFrame`）均会校验调用线程，防止跨线程操作导致的显存损坏。
- **Android**:
  - 渲染：依赖 `glThreadDispatcher` 在特定的 GL 线程顺序执行。
  - 业务：协程在 `Dispatchers.Default` 执行非 GL 任务。
- **iOS**:
  - 使用 Swift `actor` (VideoFilterManager) 确保外部 API 调用串行化。
  - `FilterEngineWrapper` 负责进入 EAGL 线程上下文并进行资源清理。

---

## 7. 性能与稳定性策略

1. **DecoderPool LRU**: 避免开启过多硬解导致系统 Jetsam 杀进程。
2. **GPU OOM 防护**: 通过 `FrameBufferPool` 的显存预算强制执行淘汰。
3. **软件解码回退**: 当硬解在高频 Seek 或倒放场景失败时，自动切换至软解确保稳定性。
4. **AV Sync**: 以 Audio Master Clock 为基准，强制对齐视频 PTS。
