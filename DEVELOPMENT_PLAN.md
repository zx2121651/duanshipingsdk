# 短视频 SDK 架构分析与未来开发计划书

本文档基于对当前 `core/`、`android/` 和 `ios/` 目录下的代码分析，详细梳理了项目的现有架构体系，并针对未来的技术演进方向，制定了一步一步的详细开发计划。

---

## 阶段一：现有架构盘点与稳固 (Phase 1: Architecture Review & Stabilization)

### 1. 核心架构模式分析
当前项目采用严谨的 **“C++ Core + 平台桥接层 (Facade)”** 架构，实现了音视频算法的核心资产保护与跨平台复用。
*   **Producer-Processor-Consumer 模型**：
    *   **采集端**：Android 端使用 CameraX 输出 OES Texture；iOS 使用 AVCaptureSession 输出 `CVPixelBuffer`。
    *   **处理端**：C++ 层 `FilterEngine` 接管，通过 `PipelineGraph` 构建节点拓扑流，执行 FBO 离屏渲染。
    *   **输出端**：Android 绑定 MediaCodec Input Surface，iOS 使用 AVAssetWriter 进行硬编码。
*   **平台线程安全策略**：
    *   Android 使用 Kotlin Coroutines 和专门分配的 GL 单线程调度器 (`glThreadDispatcher`) 保证上下文安全。
    *   iOS 深度结合 Swift 的 `actor` 机制，将 OpenGL ES Context 的访问严格串行化，避免跨线程崩溃。

### 2. 当前核心机制与优势
*   **FBO 内存池化 (`FrameBufferPool`)**：解决了移动端高帧率下频繁申请/销毁帧缓冲带来的内存抖动问题。
*   **硬件能力嗅探 (`GLContextManager`)**：引擎启动时主动嗅探 FP16、ASTC 和 Compute Shader 支持情况，具备“降级运行（Degraded Mode）”的能力。
*   **Timeline 非编引擎**：实现了 `Timeline -> Track -> Clip` 的视频轨道逻辑，配合 `Compositor` 完成转场与多轨混音 (`AudioMixer`)。
*   **零拷贝机制 (iOS)**：借助 `CVOpenGLESTextureCache`，实现了硬件 PixelBuffer 与 GL 纹理的零拷贝转换。

---

## 阶段二：近期技术优化计划 (Phase 2: Short-term Optimizations)

目标：消除技术债，统一代码规范，提升现有架构的健壮性。

### 步骤 2.1：Shader 统一化与资产管理改造
*   **现状**：Shader 代码散落于 C++ 字符串硬编码以及 `/assets/shaders/` 目录下，难以维护和热更。
*   **开发任务**：
    1.  开发 `ShaderCompiler` 工具链，将所有 GLSL 代码统一收拢到独立的着色器目录。
    2.  利用 CMake 的预编译脚本，在编译期将着色器转换为头文件，或统一打包进资源的 `assets` 包。
    3.  统一 `IAssetProvider` 接口，确保两端 (Android/iOS) 加载 Shader 的路径和错误处理行为一致。

### 步骤 2.2：错误处理闭环与可观测性建设
*   **现状**：虽然 C++ 定义了 `GLTypes.h` 中的 `ErrorCode`，但上层映射有时采用吞没异常或默认 fallback 的处理方式。
*   **开发任务**：
    1.  梳理 JNI (`NativeBridge.cpp`) 和 Objective-C++ (`FilterEngineWrapper.mm`) 的所有接口，确保任何 `-10xx` 到 `-30xx` 的错误都能抛出给宿主语言。
    2.  增强 `MetricsCollector`（目前倾向于使用 `std::deque` 的滑动窗口）：
        *   记录并上报**渲染耗时**（FPS 均值与 P99 耗时）。
        *   记录 **FBO 命中率**和**丢帧率**。
    3.  触发熔断：当 FPS 连续下降时，触发 `FilterEngineState::DEGRADED` 事件，上层 UI 提示“设备过热，已降低画质”。

---

## 阶段三：中期核心架构演进 (Phase 3: Mid-term Architecture Evolution)

目标：打破目前“实时录制”和“后期编辑”的壁垒，实现流水线的极致统一。

### 步骤 3.1：Timeline 节点化 (Pipeline Integration)
*   **现状**：当前的实时预览流 (`FilterEngine`) 与非编流 (`Compositor` / `Timeline`) 的集成度不够深，属于两套平行流程。
*   **开发任务**：
    1.  将 `Timeline` 整体封装为一个继承自 `PipelineNode` 的超级节点。
    2.  统一 Pull-model：所有渲染请求统一由终端的 Output Node 发起时间戳 `pullFrame(timestampNs)`，倒推 Timeline 或 Camera 节点提供画面。
    3.  实现画中画、分屏等复杂特效：通过构建有向无环图（DAG），利用 `PipelineGraph` 的 `connect` 实现多输入源的自由组合。

---

## 阶段四：长期底层渲染革命 (Phase 4: Long-term Rendering Revolution)

目标：摆脱 OpenGL ES 历史包袱，拥抱现代图形 API。

### 步骤 4.1：抽象 RHI (Render Hardware Interface) 层
*   **现状**：代码深度绑定 OpenGL ES 3.0/3.1，而 iOS 已废弃 OpenGL 转向 Metal，Android 的未来在 Vulkan。
*   **开发任务**：
    1.  定义纯虚接口 `IRenderDevice`、`ICommandList`、`ITexture`、`IFrameBuffer`。
    2.  将现有的 GL 代码剥离并实现为 `GLRenderDevice`。
    3.  保证上层滤镜逻辑（Filter）和图结构（PipelineGraph）不再出现任何 `glXXX` API 调用。

### 步骤 4.2：引入 Vulkan (Android) 与 Metal (iOS) 后端
*   **开发任务**：
    1.  **SPIR-V 交叉编译**：使用 glslangValidator 将现有的 GLSL Shader 离线编译为 SPIR-V 字节码。
    2.  **iOS Metal 实现**：编写 `MetalRenderDevice`，使用 SPIRV-Cross 将 SPIR-V 转译为 MSL（Metal Shading Language），利用 Metal 强大的 Compute Shader 提升模糊和 LUT 算法的性能。
    3.  **Android Vulkan 实现**：编写 `VulkanRenderDevice`，原生消费 SPIR-V，实现更低的 CPU 驱动开销和更精细的多线程 Command Buffer 录制。

---

## 总结
这份计划书从现有的稳健架构出发，规划了一条清晰的演进路线。通过近期的规范化、中期的管线统一化，以及长期的 RHI 渲染后端重构，SDK 将具备极强的商业竞争力与生命力。
