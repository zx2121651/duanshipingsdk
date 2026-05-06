# ShortVideoSDK vs 抖音SDK — V3 全面技术对标报告
> 评估日期：2026-05-06  
> 范围：P0/P1/P2 全部落地后

---

## 一、综合评分

| 维度 | V1(初始) | V2(P0~P1后) | V3(当前) | 抖音SDK 参考 |
|------|:---:|:---:|:---:|:---:|
| 架构设计 | B+ | A- | **A-** | A |
| 功能完整性 | C+ | B- | **B** | A |
| 性能 | C | B | **B+** | A |
| 稳定性 | C+ | B+ | **A-** | A |
| **综合** | **C+** | **B** | **B+** | **A** |

---

## 二、已完成全部清单（累计 16 项）

### P0 核心紧急修复 ✅（6 项）
| ID | 内容 | 文件 |
|----|------|------|
| P0-1 | 音频 Mono→Stereo Bug 修复 | `VideoEncoder.kt` channels=2 |
| P0-2 | EOS 硬等待改为 flag 精确等待 | `CountDownLatch` + `AtomicBoolean` |
| P0-3 | MediaCodec 硬件视频解码器 | `VideoDecoderAndroid.cpp` |
| P0-4 | AudioDecoder (MediaExtractor+Codec) | `AudioDecoderAndroid.cpp` |
| — | AudioDecoder 错误码规范化 | `ERR_AUDIO_*` (-3011~-3016) |
| — | GaussianBlurFilter draw call 补全 | `Filters.cpp:221-242` |

### P1 重要功能缺失 ✅（6 项）
| ID | 内容 | 文件 |
|----|------|------|
| P1-1 | H.265/HEVC 编码支持 | `VideoEncoder` + `useHevc` |
| P1-2 | 3D LUT (64³) 滤镜 | `LUT3DFilter` GLES 3.0 3D texture |
| P1-3 | 解码预取队列 | 8 帧 ring-buffer + `prefetchFrame()` |
| P1-4 | 倒放支持 (reverse playback) | `Clip::setReversed()` 全链路 |
| P1-5 | 字幕/贴纸轨道类型 | `TrackType::SUBTITLE/STICKER` |
| P1-6 | GL Context Lost 恢复机制 | `FilterEngine::onContextLost/Restored` |

### P2 体验补全 ✅（4 项）
| ID | 内容 | 文件 |
|----|------|------|
| P2-1 | 转场库 2→10 种 | `Transition.h` + `Clip.h` + `TimelineManager.kt` |
| P2-2 | 草稿序列化 (save/load) | `TimelineDraft.h` + JNI + Kotlin API |
| P2-3 | 字幕/贴纸渲染器 | `ITextRasterizer.h` + `SubtitleClip.h` + Compositor Overlay Pass |
| P2-4 | NightVisionFilter fallback 空指针修复 | `Filters.cpp` drawQuad + unbind |

---

## 三、剩余未实现功能 / Stub 清单

### 🔴 3.1 软件解码器（FFmpeg）— 最大格式兼容性缺口
**位置**：`core/include/timeline/VideoDecoder.h:49`  
**状态**：`SoftwareVideoDecoder` 全部方法返回 `ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED(-3009)`  
**影响**：VP9、AV1、ProRes 等格式硬件解码失败后无降级，直接报错。  
**抖音状态**：FFmpeg libavcodec 软解保证 99.9% 格式覆盖。  
**工作量**：~2 周（FFmpeg 交叉编译 + Android NDK 集成 + 颜色空间转换）

### 🟠 3.2 音频特效管线
| 功能 | 状态 | 抖音 | 工作量 |
|------|------|------|--------|
| 变声（音调变换） | ❌ | ✅ | 3 天（SoundTouch / Sonic 集成） |
| 降噪（ANS） | ❌ | ✅ WebRTC ANS | 3 天 |
| 节拍检测 / 音乐踩点 | ❌ | ✅ 自研 | 2 周 |
| 帧级精确淡入淡出包络 | ❌ | ✅ | 2 天 |

### 🟠 3.3 导出能力扩展
| 功能 | 状态 | 抖音 | 工作量 |
|------|------|------|--------|
| 后台导出 Service (WorkManager) | ❌ | ✅ | 3 天 |
| 分段导出（断点续传） | ❌ | ✅ | 1 周 |
| 可变帧率（VFR） | ❌ | ✅ | 1 周 |
| HDR (HLG/PQ) | ❌ | ✅ Android 10+ | 2 周 |
| 导出进度通知栏 | ❌ | ✅ | 2 天 |

### 🟠 3.4 RHI 多后端
| 后端 | 状态 | 备注 |
|------|------|------|
| GLES 3.0 | ✅ 完整 | 当前唯一可用后端 |
| Vulkan | ❌ 无 | Android 12+ 旗舰机性能 +30% |
| Metal | ❌ Stub | `GLContextManager.cpp:97` "Stub" 标记 |
| `GLRenderDevice.cpp:365` | ⚠️ Non-Android OES stub | 桌面 mock 环境不影响生产 |

### 🟠 3.5 代码级 Stub（已知技术债）
| 位置 | 内容 | 风险等级 |
|------|------|---------|
| `Filters.cpp:486` | `// stub bind texture`（NightVision RHI 纹理绑定） | 低（fallback 分支已修复 draw call） |
| `GLRenderDevice.cpp:365` | `// Non-Android stub`（OES 纹理工厂） | 无（仅桌面编译用） |
| `AudioDecoderAndroid.cpp:282` | `Non-Android stub factory` | 无（跨平台兼容层） |

### ⚪ 3.6 AI 特效层（抖音核心差异化）
| 功能 | 状态 | 工作量 |
|------|------|--------|
| 美颜（磨皮/瘦脸/大眼） | ❌ | 4 周（需 NCNN/MNN 推理框架） |
| 人像分割 / 背景替换 | ❌ | 4 周 |
| 人脸关键点追踪（106点） | ❌ | 2 周 |
| AR 贴纸（3D 跟脸） | ❌ | 3 周 |
| 绿幕抠像 | ❌ | 1 周 |
| 视频防抖（EIS） | ❌ | 2 周 |

---

## 四、抖音 SDK 核心能力雷达图（5分制）

```
                编码性能  ████░ 4.0/5  (H.264/H.265/硬件加速 ✅)
               解码兼容  ██░░░ 2.5/5  (FFmpeg 软解缺失 🔴)
                渲染管线  ████░ 4.0/5  (GLES 完整，缺 Vulkan/Metal)
                转场特效  ███░░ 3.0/5  (10 种 vs 抖音 200+)
               字幕贴纸  ████░ 4.0/5  (轨道+渲染器已通，缺 Freetype 实现)
                音频处理  ██░░░ 2.5/5  (混音 ✅，缺变声/降噪/踩点)
              草稿持久化  ████░ 4.0/5  (svdk 格式已通，缺云同步/版本迁移)
              导出稳定性  ████░ 4.0/5  (精确 EOS ✅，缺后台/断点/HDR)
                AI 能力  ░░░░░ 0.0/5  (完全空白)
```

---

## 五、立即可做的下一步（< 3 天）

1. **音频变声集成**（SoundTouch 库，~2 天）
   - 在 `AudioMixer.cpp` 混音后增加 pitch-shift pass
   - 暴露 `setPitchShift(float)` API 到 Kotlin

2. **后台导出 Service**（~2 天）
   - Android `WorkManager` 封装 `TimelineExporter`
   - 前台通知栏进度 `NotificationManager`

3. **帧级音频淡入淡出**（~1 天）
   - `Clip::getVolume()` 从当前静态值改为包络插值
   - 预计算 fade-in/out 增益曲线在 `AudioMixer`

---

## 六、中长期路线（2~8 周）

| 优先级 | 任务 | 工作量 | 理由 |
|--------|------|--------|------|
| 🔴 P2-5 | FFmpeg 软解集成 | 2 周 | 格式兼容是底线，否则很多素材无法导入 |
| 🟠 P2-6 | Vulkan 后端 | 3 周 | 旗舰机性能差异化关键 |
| 🟠 P2-7 | 音频变声+降噪 | 1 周 | 低成本高收益 |
| 🟠 P2-8 | 后台导出 | 3 天 | 用户体验刚需 |
| ⚪ P3-1 | AI 美颜/分割 | 4~6 周 | 抖音核心差异化，需独立框架选型 |
| ⚪ P3-2 | HDR 导出管线 | 2 周 | 高端受众有限，暂缓 |

---

## 七、结论

经过三轮迭代（V1→V2→V3），ShortVideoSDK 从 **C+（不可生产）** 提升至 **B+（可商用）** 水位：

- ✅ 核心编解码管线正确且完整
- ✅ 渲染管线稳定（GL Context 恢复、错误码体系、draw call 全修复）
- ✅ 产品功能闭环（草稿保存/恢复、转场、字幕轨道）
- ⚠️ 与抖音 SDK（A 级）的 **主要差距** 已从"基础功能缺失"转变为"**AI 特效生态** + **音频高级处理** + **多后端渲染**"
- 🔴 **唯一必须立即补的硬缺口**：FFmpeg 软解（非标准格式无法导入）

**建议下一里程碑**：完成 FFmpeg 软解 + 音频变声/降噪后，SDK 可达 **A-** 商用水准。
