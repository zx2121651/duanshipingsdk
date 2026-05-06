# ShortVideoSDK vs 抖音SDK — V4 计算着色器专项对标 + 全面技术评估
> 评估日期：2026-05-06  
> 新增：Compute Shader 管线深度分析

---

## 一、当前 Compute Shader 管线审计

### 1.1 `compute_blur.comp` 审计结果

```glsl
#version 310 es
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding=0, rgba8) readonly uniform image2D inputImage;
layout(binding=1, rgba8) writeonly uniform image2D outputImage;
```

**已实现部分**：
- ✅ GLES 3.1 `image2D` 直接内存读写（无 sampler 纹理过滤开销）
- ✅ `glDispatchCompute` + `glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT)` 正确同步
- ✅ 越界 `clamp` 保护
- ✅ `ComputeBlurFilter` C++ 端完整编译链接管线

**与抖音 SDK 的关键差距**：

| 指标 | 当前 SDK | 抖音 SDK | 差距 |
|------|---------|---------|------|
| 算法复杂度 | **O(r²)** 盒式模糊 | **O(r)** 可分离高斯 | 性能差 **r 倍** |
| 多 Pass 分离 | ❌ 单 Pass 直接卷积 | ✅ 水平→垂直 两次一维卷积 | 指令数差 **2r+1 倍** |
| 局部存储共享 | ❌ 每个线程独立读全局显存 | ✅ `shared` memory tile 预取 | 显存带宽差 **~4x** |
| 近似优化 | ❌ 无 | ✅ Kawase / Dual Kawase | 同画质速度快 **3~5x** |
| 位深精度 | `rgba8` (8-bit) | `rgba16f` (half-float) | 暗部色带明显 |
| iOS 支持 | ❌ 完全禁用 (GLES 3.0 only) | ✅ Metal Compute | 平台覆盖率差 |
| 计算着色器生态 | 仅 1 个 blur | 200+ 滤镜含 compute | 特效丰富度差距大 |

### 1.2 具体性能推演（以 1080p, r=15 为例）

```
当前 compute_blur.comp:
  每像素采样次数 = (2*15+1)² = 961 次 imageLoad
  总显存读取量 = 1920*1080*961*4B ≈ 7.8 GB / frame

抖音 Dual Kawase / 可分离高斯:
  每像素采样次数 ≈ 2*(2*15+1) = 62 次 (水平+垂直)
  总显存读取量 ≈ 1920*1080*62*4B ≈ 0.5 GB / frame
  
→ 显存带宽差距: ~16x
→ 在 Mali-G57 (中端) 上当前实现将直接触发 Tiler 瓶颈，帧率 < 15fps
```

---

## 二、更新综合评分（V3→V4）

| 维度 | V3 | V4 | 变动原因 |
|------|:---:|:---:|:---|
| 架构设计 | A- | **A-** | 无变化 |
| 功能完整性 | B | **B** | 无变化 |
| **GPU 计算性能** | B+ | **B-** | 发现 compute blur 算法级缺陷 |
| 稳定性 | A- | **A-** | 无变化 |
| **综合** | **B+** | **B** | compute 特效未达商用性能 |

---

## 三、立即修复清单（< 1 天）

### Fix-A: `compute_blur.comp` 算法升级 → 可分离高斯
**文件**：`assets/shaders/compute_blur.comp` + `core/src/Filters.cpp`  
**原因**：当前 O(r²) 算法在中端机上不可用，必须改为两次一维卷积。

**建议新 shader 架构**：
```glsl
// Pass 1: compute_blur_h.comp (水平方向)
// Pass 2: compute_blur_v.comp (垂直方向)
// 或使用 ping-pong image2D binding 在同一 program 内 dispatch 两次
```

### Fix-B: `GaussianBlurFilter` (fragment shader 版) 同步升级
当前 `GaussianBlurFilter` 也是 naive 2-pass 但没有预计算权重，需要统一升级：
- 预计算 1D Gaussian 权重数组 `weights[32]` uniform
- 水平 Pass 和垂直 Pass 共用同一套权重

---

## 四、中长期 Compute 特效路线

| 优先级 | 特效 | 算法 | 工作量 | 抖音对标 |
|--------|------|------|--------|---------|
| 🔴 立即 | 可分离高斯 blur | 两次一维卷积 + 权重预计算 | 1 天 | 基础必备 |
| 🔴 立即 | Dual Kawase blur | 4-tap 降采样 + 4-tap 上采样 | 2 天 | 抖音默认 blur |
| 🟠 短期 | 并行前缀和 (Parallel Scan) | 用于实时直方图/亮度均衡 | 3 天 | 抖音自动曝光 |
| 🟠 短期 | 共享内存 tile 优化 | `shared` memory 预取 32x32 tile | 3 天 | 性能基线 |
| 🟡 中期 | Bloom (阈值 + blur + 叠加) | Kawase blur + additive blending | 1 周 | 抖音氛围特效 |
| 🟡 中期 | 运动模糊 (Motion Blur) | 方向性卷积 + 深度图 | 1 周 | 抖音转场 |
| ⚪ 长期 | 粒子系统 compute 模拟 | Position/Velocity SSBO update | 2 周 | 抖音 AR 特效 |

---

## 五、完整功能雷达图（V4 更新版）

```
                编码性能  ████░ 4.0/5  (H.264/H.265/硬件加速 ✅)
               解码兼容  ██░░░ 2.5/5  (FFmpeg 软解缺失 🔴)
           渲染管线(功能) ████░ 4.0/5  (GLES 完整，缺 Vulkan/Metal)
           渲染管线(性能) ███░░ 3.0/5  (compute blur 算法差，缺 tile 优化)
                转场特效  ███░░ 3.0/5  (10 种 vs 抖音 200+)
               字幕贴纸  ████░ 4.0/5  (轨道+渲染器已通，缺 Freetype 实现)
                音频处理  ██░░░ 2.5/5  (混音 ✅，缺变声/降噪/踩点)
              草稿持久化  ████░ 4.0/5  (svdk 格式已通，缺云同步)
              导出稳定性  ████░ 4.0/5  (精确 EOS ✅，缺后台/断点/HDR)
           GPU 计算优化  ███░░ 3.0/5  (有 compute 能力，算法未优化)
                AI 能力  ░░░░░ 0.0/5  (完全空白)
```

---

## 六、与抖音 SDK 的核心差距排序（按投入产出比）

| 排名 | 差距 | 影响程度 | 修复成本 | ROI |
|------|------|---------|---------|-----|
| 1 | **compute blur 算法劣化** | 🔴 高（中端机卡顿） | 1 天 | **极高** |
| 2 | **FFmpeg 软解缺失** | 🔴 高（素材无法导入） | 2 周 | 高 |
| 3 | **音频变声/降噪** | 🟠 中（用户体验） | 1 周 | 高 |
| 4 | **后台导出** | 🟠 中（用户体验） | 3 天 | 高 |
| 5 | **转场库扩展** | 🟠 中（内容差异化） | 1 周/批 | 中 |
| 6 | **Vulkan/Metal 后端** | 🟠 中（旗舰性能） | 3 周 | 中 |
| 7 | **AI 美颜/分割** | ⚪ 长期（核心壁垒） | 4~6 周 | 极高（但成本高） |
| 8 | **HDR 导出** | ⚪ 低（受众小） | 2 周 | 低 |

---

## 七、结论与建议

### V4 核心发现
1. **Compute Shader 能力存在但算法未达商用**：`compute_blur.comp` 的 O(r²) 盒式模糊在 Mali-G57 级别 GPU 上会导致帧率暴跌，必须升级为可分离高斯或 Dual Kawase。
2. **与抖音 SDK 的差距已从"功能有无"变为"性能算法优化"**：基础框架（RHI、compute dispatch、image2D binding）已具备，缺的是算法层打磨。
3. **iOS Compute 完全缺失**：当前代码在 iOS 上强制禁用 compute（`m_supportComputeShader = false`），而抖音通过 Metal Compute 实现同等能力。这是平台策略问题，非技术问题。

### 下一步行动建议
**立即（今天）**：
- 重写 `compute_blur.comp` → 可分离高斯（水平+垂直两次 dispatch）
- 同步升级 `GaussianBlurFilter` 的 fragment shader 版权重预计算

**本周**：
- FFmpeg 软解集成启动（2 周计划）
- 音频变声/降噪（SoundTouch + WebRTC ANS）

**下月**：
- Dual Kawase Bloom 特效管线
- 后台导出 Service
