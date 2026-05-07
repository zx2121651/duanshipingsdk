# ShortVideo SDK 项目代码与架构分析

## 1. 项目整体架构

该项目整体采用 **C++ Core + 平台桥接层（Android/iOS）** 的跨平台 SDK 架构：

- `core/`：滤镜引擎、OpenGL 资源管理、Pipeline Graph、Timeline/Compositor。
- `android/`：Kotlin Facade + JNI（C++）桥接，接 CameraX/MediaCodec/Oboe。
- `ios/`：Swift Facade（actor）+ Objective-C++ Wrapper，接 AVFoundation/VideoToolbox。

该分层可以较好地实现“算法核心跨平台复用 + 平台能力按端适配”的目标。

---

## 2. Core 层分析（C++）

### 2.1 FilterEngine 职责与执行模型

`FilterEngine` 负责：

1. 初始化与线程绑定（`ThreadCheck`）。
2. 能力探测（`GLContextManager`）。
3. 逐滤镜处理（Texture → FBO → Texture）。
4. 参数广播与资源释放。

在中间滤镜阶段会按设备能力选择 FBO 精度（FP16/RGB565/RGBA8888），体现了明显的分级降级策略。

### 2.2 Pipeline Graph（节点图）

Core 中不仅有串行滤镜链，还引入了基于节点的 Graph 模式：

- `PipelineNode` 提供 pull-model 抽象。
- `PipelineGraph` 提供建图、环检测、拓扑排序、sink 拉取执行。

这说明项目在架构上具备从“线性滤镜链”向“可拓扑扩展处理图”演进的基础。

### 2.3 Timeline 合成链

`Compositor::renderFrameAtTime` 的主流程清晰：

1. 拉取时间点活跃 clip。
2. 从解码池取帧。
3. 做 copy/blend/transition。
4. 走 `FilterEngine` 做后处理。
5. 输出到目标 FBO。

该链路设计满足 NLE（非线性编辑）基本能力。

---

## 3. Android 层分析

### 3.1 VideoFilterManager（Facade）

Android 端 `VideoFilterManager` 使用：

- `StateFlow` 暴露引擎状态。
- `SharedFlow<Result<Int>>` 抛出处理纹理。
- `glThreadDispatcher` 将 GL 操作强制收敛到渲染线程。

这是较为合理的“业务层/渲染层解耦 + 并发安全”设计。

### 3.2 RenderEngine + NativeBridge

- `RenderEngine.kt` 负责 JNI 入口、SurfaceTexture/OES 驱动、参数/录制接口暴露。
- `NativeBridge.cpp` 负责将 OES 输入喂给 `FilterEngine`，并可将输出渲染到 MediaCodec 输入 Surface。
- 同时桥接 Oboe 音频采集，形成音视频录制路径。

---

## 4. iOS 层分析

### 4.1 Swift Facade（actor）

iOS `VideoFilterManager` 使用 `actor` 进行状态与调用串行化，并用 `AsyncStream` 输出处理结果，符合实时视频链路的并发安全诉求。

### 4.2 Objective-C++ Wrapper

`FilterEngineWrapper.mm` 使用 `CVOpenGLESTextureCache` 将 `CVPixelBuffer` 映射为 GL 纹理，并通过 blit 将结果回写至 `CVPixelBufferPool`，在减少拷贝与保持实时性之间取得了较好平衡。

---

## 5. 错误处理模型

项目定义了统一错误码字典（初始化/渲染/Timeline 分段）与 `Result` 封装，Android JNI 层和 Timeline Bridge 已在使用这些错误码，整体方向正确。

建议下一步把“错误码 → 平台异常类型”的映射策略再统一（包括 iOS 侧）。

---

## 6. 主要风险与问题

### 6.1 高优先级（P0）

1. `core/src/FilterEngine.cpp` 存在可疑大括号与作用域结构异常，可能导致编译/链接问题。
2. 部分 Kotlin 文件排版与函数拼接不整齐，可维护性较差（潜在语法/审查风险）。
3. iOS `VideoFilterManager.swift` 内部对象命名引用存在不一致迹象（`engine` vs `engineWrapper`），需尽快校验。

### 6.2 中优先级（P1）

1. Graph 模式与传统滤镜链共存，建议明确唯一主路径与回退策略。
2. Timeline 目前 JNI 只暴露基础增删接口，可补齐更多编辑操作（删 clip、改速度、改转场、关键帧参数等）。

### 6.3 环境约束

当前 Linux 本地构建环境缺少 `GLES3/gl3.h`，导致 Core 构建无法完成。该问题属于环境依赖，不一定是业务代码逻辑错误。

---

## 7. 建议的改造路线图

### 阶段 A（稳定性修复）

- 修复 Core/Android/iOS 中的编译级问题（P0）。
- 增加最低限度 CI（Core 编译 + Android 编译 + iOS 编译）。

### 阶段 B（架构收敛）

- 明确 Graph 为主执行模型，串行链作为兼容模式。
- 统一性能指标采集与错误上报协议。

### 阶段 C（能力增强）

- Timeline 扩展编辑 API 与关键帧系统。
- 完善导出路径（编码参数、音视频同步、失败回退策略）。

---

## 8. 总结

该仓库的总体架构方向正确，分层清晰，技术栈匹配短视频 SDK 的常见工程形态；核心能力（滤镜链、图执行、Timeline 合成、双端桥接）都已具备雏形。当前最关键工作是先完成一轮“编译稳定性与代码一致性”清理，再推进架构收敛和能力扩展。
