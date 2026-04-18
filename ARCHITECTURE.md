# ShortVideo SDK 架构说明文档

> 文档目标：说明当前项目在 **核心渲染引擎、Android/iOS 平台适配、Timeline 编辑子系统、错误处理与性能策略** 上的整体设计与关键流程。

---

## 0. 快速上手 (Getting Started)

对于新开发者或 Agent，请首先查阅 [BUILD.md](./BUILD.md) 以获取完整的**本地构建前置条件说明**。

- **Android**: 确保安装 JDK 17 和 Android SDK/NDK。
- **iOS**: 需要 macOS 和 Xcode 环境。
- **Core C++**: 依赖 CMake 3.10.2+。

---

## 1. 项目概览

本项目采用「**C++ Core + 平台桥接层**」架构：

- `core/`：跨平台渲染管线、滤镜引擎、帧缓冲池、OpenGL 能力探测、Timeline/Compositor。
- `android/`：CameraX 采集、Kotlin Facade、JNI Bridge、MediaCodec 编码与封装。
- `ios/`：Swift Facade + Objective-C++ Wrapper，连接 C++ 核心与 AVFoundation。

该模式实现了平台能力和算法核心的分离：渲染与流水线逻辑集中在 `core`，平台层负责采集/展示/硬件编解码与线程调度。

---

## 2. 分层架构

### 2.1 Core 层（C++）

#### 2.1.1 PipelineGraph (渲染管线)

核心渲染架构已从线性滤镜链演进为**基于拉模式（Pull-model）的 PipelineGraph**：

- **PipelineNode**: 所有处理节点的基类，定义了 `pullFrame(timestamp)` 接口。
- **PipelineGraph**: 管理节点拓扑、循环检测、拓扑排序及执行驱动。
- **节点类型**:
  - `CameraInputNode`: 作为 Source，接收平台层推送的 OES/RGB 纹理。
  - `TimelineNode`: 将 Timeline/Compositor 接入管线作为输入源。
  - `FilterNode`: 包装 `Filter` 对象，负责具体的纹理处理。
  - `OutputNode`: 作为 Sink，收集最终渲染结果。

#### 2.1.2 FilterEngine（管线总控）

职责：
1. **初始化**: 绑定渲染线程、触发 `GLContextManager` 能力嗅探。
2. **动态构建**: 根据业务场景（预览/时间线编辑）动态组装 `PipelineGraph`。
3. **参数广播**: 遍历管线节点，将全局参数（如滤镜强度）下发给对应的 `FilterNode`。

#### 2.1.3 GLContextManager（硬件能力探测）

在 GL Context 就绪后执行 capability sniffing：
- FP16 Render Target 支持。
- Compute Shader 支持（校验 `GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS`）。
- 根据探测结果自动配置 `FilterNode` 的中间 FBO 精度（`FP16 / RGB565 / RGBA8888`）。

#### 2.1.4 FrameBufferPool

- 实现**全局显存预算管理**（默认 256MB）。
- 采用 **LRU 淘汰策略**，按 `(width, height, precision)` 复用 FBO。
- `VideoFrame` 持有 `FrameBuffer` 的智能指针，确保资源在帧生命周期结束后自动归还。

---

### 2.2 Android 层

#### 2.2.1 VideoFilterManager

职责：
- 封装 `RenderEngine` (JNI) 并提供协程友好的 API。
- **线程模型**: 使用 `glThreadDispatcher` 确保所有 GL 操作收敛到单一渲染线程。
- **背压控制**: `SharedFlow` 采用 `DROP_OLDEST` 策略防止渲染积压。

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
OutputNode::pullFrame(t)
    -> FilterNode::pullFrame(t)
        -> upstreamNode::pullFrame(t) (Recursively)
            -> CameraInputNode / TimelineNode (Provide Source Frame)
        -> Filter::processFrame (Draw to FBO)
    -> Final Texture Output
```

### 3.2 Android 导出/录制链

```text
PipelineGraph Output
    -> Blit to MediaCodec Input Surface
    -> MediaMuxer (AAC + AVC)
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

---

## 6. 并发与线程模型

- **C++ Core**: 严格的 `ThreadCheck`。`FilterEngine` 操作必须在绑定线程执行。
- **Android**: 依赖 `glThreadDispatcher` 执行渲染，协程处理非 GL 任务。
- **iOS**: 依赖 Swift `actor` 实现 API 调用的线性化。

---

## 7. 性能与稳定性策略

1. **DecoderPool LRU**: 避免开启过多硬解导致系统 Jetsam 杀进程。
2. **GPU OOM 防护**: 通过 `FrameBufferPool` 的显存预算强制执行淘汰。
3. **软件解码回退**: 当硬解在高频 Seek 或倒放场景失败时，自动切换至软解确保稳定性。
4. **AV Sync**: 以 Audio Master Clock 为基准，强制对齐视频 PTS。
