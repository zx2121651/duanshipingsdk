# 抖音级短视频 SDK 对标分析报告

本报告基于当前代码库对 Android/C++ 视频 SDK 进行架构梳理、能力矩阵评估、风险识别与可执行路线规划，目标是判断其距离抖音级短视频 SDK 的差距。

## 1. 当前架构总览

```text
sample-android (Compose Demo / 集成验证)
        │
        ▼
:android SDK Library (Kotlin Facade)
  ├─ RenderEngine / VideoFilterManager          实时相机预览与滤镜控制
  ├─ capture/*                                  分段拍摄状态机、录制、节拍
  ├─ timeline/*                                 NLE 时间线、预览、缩略图、波形、导出 facade
  ├─ export/*                                   WorkManager 队列、断点恢复、色彩空间、后台服务
  ├─ ai/*                                       模型资产、版本、OTA、delegate 策略、AI 控制器
  └─ CrashReporter                              Crash/ANR 本地观测
        │ JNI
        ▼
android/src/main/cpp
  ├─ NativeBridge.cpp                           RenderEngine / AI / RHI / 采集 JNI
  ├─ TimelineBridge.cpp                         Timeline 编辑 JNI
  ├─ TimelinePreviewBridge.cpp                  GL 线程预览 JNI
  └─ TimelineExporterBridge.cpp                 Native exporter JNI
        │
        ▼
core C++
  ├─ FilterEngine + PipelineGraph               渲染图、滤镜、FBO 池、性能指标
  ├─ RHI                                        GLES/Vulkan/Metal 抽象
  ├─ timeline                                   Timeline/Track/Clip/Compositor/DecoderPool/Exporter
  ├─ ai                                         TFLite 推理、美颜、美妆、人脸、分割、贴纸
  └─ tests                                      C++ 单测、兼容、导出、性能基准
```

## 2. 模块能力矩阵

| 模块 | 当前状态 | 证据路径 | 对标评价 |
| --- | --- | --- | --- |
| 实时相机渲染 | 已有核心闭环 | `RenderEngine.kt`, `NativeBridge.cpp`, `FilterEngine.h` | 可用，但需要更多真机首帧/丢帧/发热数据 |
| 滤镜管线 | 已有较完整骨架 | `FilterEngine`, `Filters`, `PipelineGraph`, `ShaderManager` | 接近基础 SDK，距抖音级道具协议仍远 |
| RHI | GLES + Vulkan/Metal 抽象存在 | `core/include/rhi`, `RenderDeviceFactory` | 架构先进，但跨后端一致性与自动测试不足 |
| 分段拍摄 | 有状态机和 recorder | `capture/CaptureStateMachine.kt`, `SegmentRecorder.kt` | 需要端到端录制、删除、合并、崩溃恢复验证 |
| Timeline 编辑 | 已具备多轨/clip/transition/keyframe | `TimelineManager.kt`, `TimelineBridge.cpp`, `Clip.h` | 基础 NLE 已成型，模板化/高级曲线/批量编辑缺失 |
| Timeline 预览 | 已支持 GL 预览、seek、scrub | `TimelinePreviewBridge.kt/.cpp`, `DecoderPool.h` | 方向正确，需量化 seek 延迟与缓存命中率 |
| 缩略图/波形 | 已有异步缓存链路 | `ThumbnailExtractor.kt`, `WaveformGenerator.kt` | 可作为编辑 UI 基础，需大素材压力测试 |
| 导出 | Native exporter + Android 队列并存 | `TimelineExporter.h`, `TimelineExporter.kt`, `ExportQueue.kt` | Native 能力存在，但 `ExportQueue` 默认执行器仍是壳 |
| 断点恢复 | 有 checkpoint 管理 | `ExportRecovery.kt` | 需与真实 native export progress/chunk 绑定验收 |
| 色彩空间 | 有 MediaFormat 配置 | `ColorSpaceConfig.kt` | HDR/SDR 配置起步，缺真实 HDR 测试素材与设备验证 |
| AI 模型管理 | 有资产、版本、OTA、delegate 策略 | `ai/Model*.kt`, `TfliteDelegateStrategy.kt` | 框架完备，但真实模型/灰度/回滚/性能策略仍需落地 |
| AI 特效 | 美颜、美妆、人脸、分割类存在 | `core/include/ai/*`, `NativeBridge.cpp` | API 初具雏形，实时多人脸、稳定跟踪、道具生态不足 |
| 特效包 | 有 EffectPlugin 管理 | `EffectPlugin.h/.cpp`, `EffectPackageInstaller.kt` | 需要资源协议、签名校验、版本兼容和设计工具链 |
| Crash/ANR | 有本地 reporter | `CrashReporter.kt` | 已具备本地观测，需 sample/宿主初始化和远端 sink 接入 |
| CI | 多 workflow + 单测 | `.github/workflows/*`, `tests/*` | 覆盖面广，但存在 workflow 重复和 target/class 不匹配风险 |
| 设备矩阵 | 有 Firebase Test Lab 配置 | `android-device-matrix.yml`, `DeviceMatrixSmokeTest.kt` | 已起步，缺完整机型分层、性能 gate、失败归因 |

## 3. 主要差距

### 3.1 功能差距

- **道具生态不足**：缺统一道具包 manifest、素材依赖图、shader/模型/贴纸版本兼容、签名校验、热加载回滚。
- **AI 能力仍是框架化**：真实 TFLite 模型缺失，GPU/NNAPI/XNNPACK 策略没有覆盖所有 AI filter，缺多人脸跟踪与时序平滑验收。
- **模板化能力缺失**：抖音级 SDK 通常需要模板、一键成片、音乐卡点、自动字幕、素材替换规则。
- **导出队列未完全闭环**：`ExportQueue` 使用 `ExportExecutor` 注入真实实现，默认仍返回失败，不能单独完成后台导出。

### 3.2 性能差距

- **预览性能缺少自动 gate**：已有 P90/P99 指标和诊断页，但 CI 未形成预览帧耗、丢帧、内存峰值阈值。
- **seek/scrub 无量化指标**：实现存在，但缺 P50/P90 seek latency、缓存命中率和 exact seek 成功率。
- **AI delegate 策略未闭环**：Kotlin 侧有选择策略，Native 侧需要覆盖所有 TFLite-backed 模块并记录 fallback 原因。

### 3.3 稳定性差距

- **Native handle 生命周期风险**：JNI 使用裸 `jlong`，需要系统化防 double-free、use-after-release、跨线程调用。
- **CI 配置存在漂移**：`ci.yml` 曾引用不存在的 `run_tests` target 和 benchmark class，需要统一 workflow。
- **CrashReporter 未默认接入 sample**：模块存在但如果宿主不初始化，Crash/ANR 观测不会生效。

### 3.4 SDK 产品化差距

- **API 边界不够清晰**：部分能力挂在 `InternalApi` 或多套导出 API 并存，宿主如何选择不够明确。
- **发布 gate 不完整**：缺 ABI 体积、AAR publishing、ProGuard/R8 keep 规则、符号文件、release note 自动化。
- **文档不足**：需要公开 API cookbook、线程模型、错误码、资源包协议、性能调优指南。

## 4. 高优先级改造路线图

### P0：构建与质量门禁修复

- 修复 CI 中不存在的 CMake target / benchmark class。
- 合并或明确多个 workflow 的职责，避免重复触发与 secret 命名不一致。
- 确保 Android unit test、C++ ctest、sample assemble 在 PR 上稳定运行。
- sample 默认初始化 `CrashReporter`，让本地 crash/ANR 报告可见。

### P1：导出闭环与真机可用性

- 为 `ExportQueue` 提供 SDK 内置 native executor 或 sample 级 executor 示例。
- 将 checkpoint 的 `resumePositionMs`、chunk index 与真实 native exporter progress 绑定。
- 增加导出 E2E Android/instrumented smoke：短视频、多轨、音频、取消、恢复。
- 落地 HDR/SDR 真机验证素材和 MediaFormat 参数快照。

### P2：AI 与特效生产化

- 替换 stub 模型并补充 model manifest、checksum、灰度、回滚策略。
- TFLite delegate 覆盖 face/hair/segmentation/body/gesture，记录 fallback 链路。
- 定义 EffectPackage manifest：shader、texture、model、version、minSdk、capability flags。
- 增加道具包安装、卸载、热切换、损坏包回滚测试。

### P3：抖音级体验增强

- 模板系统：轨道布局、素材槽位、卡点节拍、自动转场、字体/贴纸资源。
- 音乐能力：节拍检测、变速不变调、踩点预览、版权/缓存接口。
- 高级 AI：多人脸稳定跟踪、姿态/手势驱动贴纸、分割边缘 refinement。
- 性能云控：按设备 tier 下发滤镜质量、AI delegate、分辨率、导出策略。

## 5. 上线质量清单

- **Build Gate**：`:android:testDebugUnitTest`、`:android:assembleDebug`、`:sample-android:assembleDebug`、C++ `ctest`。
- **Device Gate**：Firebase lite/standard/full，覆盖 API 24/26/28/30/31/33/34、低中高端 SoC。
- **Preview Gate**：首帧 < 500ms，P90 frame time < 16.7ms（60fps）或 < 33.3ms（30fps），连续 scrub 无崩溃。
- **Export Gate**：10s/60s 多轨导出成功率、音画同步误差、取消/恢复、断点续导、分片完整性。
- **AI Gate**：模型 checksum、加载耗时、delegate fallback、内存峰值、低端机降级。
- **Crash Gate**：Java crash、Native error、ANR、本地报告、远端 sink、符号化文件。
- **Release Gate**：AAR、ABI、R8 keep、API 文档、sample 可运行、版本说明、兼容矩阵。

## 6. 当前建议结论

当前项目已经不是简单 demo，而是具备短视频 SDK 的核心骨架：实时渲染、NLE 时间线、预览、导出、AI、FFmpeg/TFLite 可选集成、测试与设备矩阵均已起步。距离抖音级 SDK 的主要差距不在单点功能，而在生产化闭环：真实模型/真实设备/真实导出恢复/真实性能 gate/资源包生态/稳定性观测。下一阶段应优先收敛 P0/P1，把“已有骨架”变成“端到端可验收能力”。
