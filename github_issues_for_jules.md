# GitHub Issues for Google Jules — 短视频SDK开发任务（v3 适配版）

> 设计原则：每个Issue 3-6个步骤，聚焦单一模块，标注前置依赖，可独立编译和测试。
> 优先级：P0(必须) → P1(重要) → P2(优化)
> **Jules模型**：Gemini 3.1 Pro
>
> **Jules 能力边界说明**：
> - **擅长**：修复bug、重构代码、添加测试、实现已有接口的类、升级依赖、添加文档注释、JSON/YAML配置、基于现有代码模式的扩展
> - **不擅长**：需要外部模型文件（.tflite）、从零设计GLSL shader、iOS真机测试、音频算法调参、性能基准测试、创造性设计
> - **需要人工辅助**：GLSL shader需人工验证视觉效果、AI模型需人工提供.tflite文件、iOS代码需Xcode环境验证

---

## Phase 1: 基础能力补齐（P0，无前置依赖）

---

### Issue #1 — [P0] FFmpegVideoDecoder: 完善 open() / close() / seekExact() 错误处理与测试覆盖

**目标**: 完善FFmpeg视频解码器的错误处理路径，补充单元测试。

**前置依赖**: 无

**涉及文件**:
- `core/src/timeline/FFmpegVideoDecoder.cpp`
- `tests/test_ffmpeg_decoder.cpp`

**Jules 适配说明**: 代码已存在（`HAS_FFMPEG_DECODER`宏控制），需要完善边界情况处理和测试覆盖。Jules 可以分析现有代码模式并补充测试。

**步骤**:
1. 在 `FFmpegVideoDecoder::open()` 中添加对 `avformat_alloc_context` 返回null的检查（已部分存在，需确认完整性）
2. 在 `seekExact()` 中添加对 `m_codecCtx` 为null的保护
3. 在 `close()` 中确保所有资源按正确顺序释放（codec → format → frame → packet → sws）
4. 在 `test_ffmpeg_decoder.cpp` 中添加测试：
   - `test_open_invalid_path`：验证无效路径返回错误码
   - `test_seek_without_open`：验证未open时seek返回错误
   - `test_double_close`：验证多次close不崩溃
   - `test_factory_returns_ffmpeg_decoder`（`HAS_FFMPEG_DECODER`时）：验证工厂返回的是FFmpeg实现而非stub

**完成标志**:
- [ ] 所有边界错误路径返回明确错误码
- [ ] `test_ffmpeg_decoder` 新增4个测试通过
- [ ] 无内存泄漏（valgrind clean）

---

### Issue #2 — [P0] DecoderPool: 实现软解回退逻辑（Software Fallback）

**目标**: 当硬件解码失败时，自动创建并使用FFmpeg软解实例。

**前置依赖**: #1

**涉及文件**:
- `core/src/timeline/DecoderPool.cpp`
- `core/include/timeline/DecoderPool.h`
- `tests/test_decoder_pool.cpp`

**Jules 适配说明**: 头文件中已有 `softDecoder` 和 `hwFailed` 字段，需要实现回退逻辑。Jules 可以基于现有代码结构完成实现。

**步骤**:
1. 在 `DecoderPool::getFrame()` 中，当 `ctx->hwFailed == true` 且 `m_strategy == AUTO` 时：
   - 如果 `ctx->softDecoder` 为空，调用 `createSoftwareDecoder_FFmpeg()` 创建实例并 `open(sourcePath)`
   - 如果创建失败，返回错误
2. 如果 `ctx->softDecoder` 已存在且已open，调用 `softDecoder->getFrameAt(localTimeNs)`
3. 在 `releaseMedia()` 中同时释放 `softDecoder`（调用 `close()` 后重置指针）
4. 在 `test_decoder_pool.cpp` 中添加测试：
   - 模拟 `hwFailed=true`，验证 `getFrame()` 尝试创建软解实例
   - 验证软解失败时返回明确错误码
   - 验证 `releaseMedia()` 正确释放软解资源

**完成标志**:
- [ ] `hwFailed=true` 时自动创建软解实例
- [ ] 软解失败时返回明确错误码
- [ ] `releaseMedia()` 正确释放软解资源
- [ ] `test_decoder_pool` 新增测试通过

---

### Issue #3 — [P0] FaceLandmarkDetector: 完善 TFLite 输出后处理（decodeLandmarks）

**目标**: 完善TFLite输出到106点人脸关键点的转换逻辑。

**前置依赖**: 无

**涉及文件**:
- `core/src/ai/FaceLandmarkDetector.cpp`
- `core/include/ai/FaceLandmarkDetector.h`
- `tests/test_ai_inference.cpp`

**Jules 适配说明**: `decodeLandmarks()` 方法已声明为静态方法，需要实现。注意：此Issue**不需要真实.tflite模型**，使用stub模式测试逻辑正确性。

**步骤**:
1. 实现 `FaceLandmarkDetector::decodeLandmarks(const float* rawOutput, int outputLen, int imgW, int imgH, FaceResult& out)`：
   - 验证 `outputLen == 212`（106点 × 2坐标）或 `outputLen == 318`（106点 × 3含置信度）
   - 按 `[x, y, score]` 每3个一组解析为106个 `FaceLandmark`
   - 坐标反归一化：将 [0,1] 范围乘以输入图像宽高
   - 如果输出只有212个值（无置信度），设置默认score=1.0
2. 置信度过滤：score < 0.5 的点标记为无效（score=0）
3. 在 `test_ai_inference.cpp` 中添加测试：
   - 构造模拟TFLite输出（212个float，覆盖全范围），验证解析为106个点
   - 验证坐标在 [0, imageWidth] × [0, imageHeight] 范围内
   - 验证置信度<0.5的点被正确过滤

**完成标志**:
- [ ] `decodeLandmarks()` 正确处理212和318两种输出格式
- [ ] 所有关键点坐标反归一化正确
- [ ] 置信度<0.5的点被正确过滤
- [ ] `test_ai_inference` 新增测试通过

**人工检查点**: 此Issue仅实现后处理逻辑，**不需要真实.tflite模型**。后续集成真实模型时需人工验证。

---

### Issue #4 — [P0] BodyPoseDetector: 实现 decodeKeypoints() 并扩展关键点索引定义

**目标**: 实现人体姿态关键点解析，为后续33点扩展做准备。

**前置依赖**: 无

**涉及文件**:
- `core/src/ai/BodyPoseDetector.cpp`
- `core/include/ai/BodyPoseDetector.h`
- `tests/test_ai_inference.cpp`

**Jules 适配说明**: 与Issue #3类似，实现已有的声明方法，使用stub模式测试。

**步骤**:
1. 实现 `BodyPoseDetector::decodeKeypoints(const float* output, int w, int h) -> PoseResult`：
   - 解析模型输出 `[1,1,17,3]` 为17个 `PoseKeypoint`
   - 每个点格式：`[y, x, score]`（注意MoveNet输出顺序是y,x,score）
   - 坐标反归一化：x × width, y × height
2. 在 `test_ai_inference.cpp` 中添加测试：
   - 构造模拟输出（17×3=51个float），验证解析为17个关键点
   - 验证坐标在图像范围内
   - 验证 `isValid()` 方法（score >= 0.3）
3. 在 `BodyPoseDetector.h` 中预留33点扩展注释（标记18-32索引为未来扩展位）

**完成标志**:
- [ ] `decodeKeypoints()` 正确解析17点输出
- [ ] 坐标反归一化正确
- [ ] `isValid()` 按score阈值工作
- [ ] `test_ai_inference` 新增测试通过

---

### Issue #5 — [P0] SegmentationFilter: 完善参数设置与模式切换

**目标**: 完善人像分割滤镜的参数接口和模式切换逻辑。

**前置依赖**: 无

**涉及文件**:
- `core/src/ai/SegmentationFilter.cpp`
- `core/include/ai/SegmentationFilter.h`
- `tests/test_ai_inference.cpp`

**Jules 适配说明**: 头文件中已有Mode枚举和参数定义，需要完善实现。不涉及真实模型推理。

**步骤**:
1. 在 `SegmentationFilter::processFrame()` 中实现参数读取：
   - 从 `m_parameters` map 读取 `"mode"`（int，对应Mode枚举）
   - 读取 `"blurStrength"`（float）
   - 读取 `"bgColor"`（uint32_t ARGB）
2. 实现 `setBgImageTexture()` 的存储和读取逻辑
3. 在 `initialize()` 中验证shader uniform位置缓存
4. 在 `test_ai_inference.cpp` 中添加测试：
   - 构造 `SegmentationFilter`（传入null engine），验证参数set/get
   - 验证Mode切换不崩溃
   - 验证 `setBgImageTexture()` 存储正确

**完成标志**:
- [ ] 4种Mode参数可正确设置和读取
- [ ] `blurStrength`、`bgColor` 参数可正确设置
- [ ] `setBgImageTexture()` 存储正确
- [ ] `test_ai_inference` 新增测试通过

---

### Issue #6 — [P0] Clip: 添加贝塞尔曲线插值类型（Bezier Easing）

**目标**: 在关键帧系统中支持Cubic Bezier缓动曲线。

**前置依赖**: 无

**涉及文件**:
- `core/include/timeline/Clip.h`
- `core/src/timeline/Clip.cpp`
- `tests/test_timeline.cpp`

**Jules 适配说明**: 纯数学计算，无外部依赖。Jules 非常擅长此类任务。

**步骤**:
1. 在 `InterpolationType` 枚举中新增 `BEZIER = 5`
2. 扩展 `KeyframeEntry` 结构，新增4个float字段：`cp1x, cp1y, cp2x, cp2y`（控制点，默认0.5,0,0.5,1即ease-in-out）
3. 在 `Clip::addKeyframe()` 新增重载，接受 `cp1x, cp1y, cp2x, cp2y` 参数
4. 实现私有静态方法 `evaluateBezier(float t, float cp1x, float cp1y, float cp2x, float cp2y) -> float`：
   - 使用牛顿迭代法求解三次贝塞尔曲线（最多8次迭代，误差<0.001）
   - 或预计算查找表（LUT）方式优化性能
5. 修改 `applyEasing()`：当 `easing == BEZIER` 时调用 `evaluateBezier()`
6. 在 `test_timeline.cpp` 中添加测试：
   - 添加BEZIER关键帧，验证插值结果在预期曲线范围内
   - 控制点(0,0,1,1)时退化为线性插值
   - 控制点(0.42,0,0.58,1)时产生ease-in-out效果

**完成标志**:
- [ ] `InterpolationType::BEZIER` 可正常使用
- [ ] 控制点(0,0,1,1)时退化为线性插值
- [ ] 控制点(0.42,0,0.58,1)时产生ease-in-out效果
- [ ] 序列化/反序列化保留贝塞尔控制点
- [ ] `test_timeline` 新增测试通过

---

### Issue #7 — [P0] TransitionRegistry: 添加 dissolve / pixelate / circle_crop 转场

**目标**: 扩展转场库，新增3种视觉效果转场。

**前置依赖**: 无

**涉及文件**:
- `core/include/timeline/Transition.h`
- `tests/test_timeline.cpp`

**Jules 适配说明**: 需要在 `Transition.h` 中添加GLSL字符串。Jules 可以编写GLSL代码，但**需要人工验证shader视觉效果**。

**步骤**:
1. 在 `TransitionType` 枚举中新增 `DISSOLVE`, `PIXELATE`, `CIRCLE_CROP`
2. 更新 `transitionTypeName()` 映射函数（在 `Clip.h` 中）
3. 在 `TransitionRegistry::registerBuiltins()` 中编写GLSL：
   - `dissolve`：使用噪声纹理+阈值，前景从噪声中"溶解"出现
   - `pixelate`：像素大小随progress从大到小变化，前景逐步清晰
   - `circle_crop`：圆形遮罩从中心向外扩展，显示前景
4. 在 `test_timeline.cpp` 中测试：3种新转场可被 `TransitionRegistry::getTransition()` 查询到

**完成标志**:
- [ ] `TransitionRegistry` 包含 `dissolve`, `pixelate`, `circle_crop`
- [ ] 每种转场GLSL语法正确，可编译
- [ ] `Clip::setInTransition()` 支持3种新类型
- [ ] `test_timeline` 新增测试通过

**人工检查点**: GLSL shader代码需要人工在真机上验证视觉效果。Jules生成的shader可能在视觉上不完美。

---

### Issue #8 — [P0] TrackType: 新增 EFFECT 轨道类型与 EffectClip 类

**目标**: 在Timeline中支持独立的全局特效轨道。

**前置依赖**: 无

**涉及文件**:
- `core/include/timeline/Track.h`
- `core/include/timeline/Timeline.h`
- `core/include/timeline/Clip.h`
- `core/src/timeline/Clip.cpp`
- `tests/test_timeline.cpp`

**Jules 适配说明**: 基于现有Track/Clip架构扩展，Jules 可以完成。

**步骤**:
1. 在 `Track::TrackType` 枚举中新增 `EFFECT`
2. 创建 `EffectClip` 类（继承 `Clip`）：
   - 新增字段 `effectType` (string) 和 `intensity` (float)
   - 构造函数调用基类构造函数
3. 在 `Timeline` 中新增 `getActiveEffectClipsAtTime(int64_t timelineNs, std::vector<ClipPtr>& outClips)` 方法
4. 在 `getActiveEffectClipsAtTime()` 中遍历所有 `TrackType::EFFECT` 轨道，收集当前时间活跃的EffectClip
5. 在 `test_timeline.cpp` 中测试：
   - 创建含EFFECT轨的Timeline
   - 验证 `getActiveEffectClipsAtTime()` 返回正确clip
   - 验证 `EffectClip` 的 `effectType` 和 `intensity` 可被读取

**完成标志**:
- [ ] `TrackType::EFFECT` 可创建
- [ ] `getActiveEffectClipsAtTime()` 正确返回当前特效clip
- [ ] `EffectClip` 的 `effectType` 和 `intensity` 可被读取
- [ ] `test_timeline` 新增测试通过

---

## Phase 2: 核心能力扩展（P1，部分有前置依赖）

---

### Issue #9 — [P1] TransitionRegistry: 添加 glitch / rotate_in / radial_wipe 转场

**目标**: 继续扩展转场库，新增3种动态效果转场。

**前置依赖**: #7

**涉及文件**:
- `core/include/timeline/Transition.h`
- `tests/test_timeline.cpp`

**Jules 适配说明**: 同Issue #7，Jules 可以编写GLSL，但需人工验证视觉效果。

**步骤**:
1. 在 `TransitionType` 枚举中新增 `GLITCH`, `ROTATE_IN`, `RADIAL_WIPE`
2. 编写GLSL：
   - `glitch`：RGB通道分离+随机水平偏移，模拟数字故障
   - `rotate_in`：前景从画面中心3D旋转进入（使用 `sin/cos` 计算旋转后的UV）
   - `radial_wipe`：从中心向外的径向擦除（使用 `atan` 计算角度，按progress展开）
3. 更新 `transitionTypeName()` 映射
4. 在 `test_timeline.cpp` 中验证3种新转场可注册和查询

**完成标志**:
- [ ] 3种新转场GLSL可编译
- [ ] `TransitionRegistry::getTransition()` 能正确查询新转场
- [ ] 无shader编译错误

**人工检查点**: GLSL shader代码需要人工在真机上验证视觉效果。

---

### Issue #10 — [P1] AudioEffects: 实现5频段均衡器（Biquad IIR）框架

**目标**: 实现基础音频均衡器框架。

**前置依赖**: 无

**涉及文件**:
- `core/include/timeline/AudioEffects.h`
- `core/src/timeline/AudioEffects.cpp`
- `tests/test_media_compat.cpp`

**Jules 适配说明**: 纯数学实现，Jules 可以完成框架代码。但**音频效果需要人工听感验证**。

**步骤**:
1. 创建 `BiquadFilter` 结构：包含5个系数 `b0,b1,b2,a1,a2` 和状态变量 `z1,z2`
2. 实现 `computeBiquadCoefficients(type, sampleRate, freq, Q, gainDb)`：支持 `PEAKING_EQ` 类型，输出5个系数
   - 参考Audio EQ Cookbook公式实现
3. 创建 `EqualizerEffect` 类：包含5个 `BiquadFilter`（对应60Hz/250Hz/1kHz/4kHz/12kHz）
4. 实现 `EqualizerEffect::process(float sample) -> float`：依次通过5个biquad滤波器
5. 在 `test_media_compat.cpp` 中添加测试：
   - 验证biquad系数计算正确（与已知参考值对比）
   - 验证直流信号（0Hz）通过时增益正确
   - 验证 `process()` 不崩溃

**完成标志**:
- [ ] 5频段EQ框架可独立调节各频段增益 [-12dB, +12dB]
- [ ] Biquad系数计算与Audio EQ Cookbook一致
- [ ] 无音频爆音（输出钳制在 [-1.0, 1.0]）
- [ ] `test_media_compat` 新增测试通过

**人工检查点**: 音频效果需要人工听感验证，Jules 无法判断音质。

---

### Issue #11 — [P1] PipelineGraph: 实现 MergeNode 多输入节点

**目标**: 支持画中画和转场场景的多纹理合并。

**前置依赖**: 无

**涉及文件**:
- `core/include/pipeline/Nodes.h`
- `core/src/pipeline/PipelineGraph.cpp`
- `tests/test_filter_graph.cpp`

**Jules 适配说明**: 基于现有PipelineNode架构扩展，Jules 可以完成。

**步骤**:
1. 在 `Nodes.h` 中新增 `MergeNode` 类（继承 `PipelineNode`）：
   - 接受2个上游输入（`m_inputs[0]` 和 `m_inputs[1]`）
   - 持有 `BlendMode` 和 `float opacity`
   - `pullFrame()` 中分别调用两个上游的 `pullFrame()`，然后返回合并后的帧
2. 修改 `PipelineGraph::compile()`：计算每个节点的入度（`m_inputs.size()`），支持多输入节点的拓扑排序
3. 在 `test_filter_graph.cpp` 中添加测试：
   - 创建含MergeNode的图
   - 验证拓扑排序结果中MergeNode在输入节点之后
   - 验证 `execute()` 不崩溃

**完成标志**:
- [ ] `MergeNode` 可接受2个上游输入
- [ ] `PipelineGraph::compile()` 正确处理多输入节点入度
- [ ] 拓扑排序结果中MergeNode在输入节点之后
- [ ] `test_filter_graph` 新增测试通过

---

### Issue #12 — [P1] ExportRecovery: 实现导出进度持久化框架

**目标**: 导出中断后可从上次进度恢复。

**前置依赖**: 无

**涉及文件**:
- 新增 `android/src/main/java/com/sdk/video/export/ExportRecovery.kt`
- `tests/test_export_e2e.cpp`

**Jules 适配说明**: 纯Kotlin/Java代码，无外部依赖。Jules 可以完成。

**步骤**:
1. 定义 `ExportCheckpoint` 数据类：`draftPath`, `outputPath`, `exportedFrames`, `totalFrames`, `timestamp`
2. 实现 `saveCheckpoint(checkpoint: ExportCheckpoint)`：序列化为JSON，写入文件系统（`context.filesDir/export_recovery/`）
3. 实现 `loadCheckpoint(draftPath: String): ExportCheckpoint?`：从文件系统读取并反序列化
4. 实现 `clearCheckpoint(draftPath: String)`：删除对应文件
5. 在 `test_export_e2e.cpp` 中模拟中断场景：
   - 创建模拟checkpoint数据
   - 验证序列化/反序列化正确
   - 验证 `clearCheckpoint()` 删除记录

**完成标志**:
- [ ] `saveCheckpoint()` 正确持久化进度到文件
- [ ] `loadCheckpoint()` 正确恢复进度
- [ ] `clearCheckpoint()` 删除记录
- [ ] `test_export_e2e` 新增测试通过

---

### Issue #13 — [P1] TemplateEngine: 创建10个视频模板JSON

**目标**: 扩充模板库。

**前置依赖**: 无

**涉及文件**:
- `assets/templates/` 目录

**Jules 适配说明**: 纯JSON配置工作，Jules 非常擅长。

**步骤**:
1. 创建 `birthday_party.json`：3个slot，欢快BGM，彩色crossfade转场
2. 创建 `wedding_elegant.json`：4个slot，淡雅LUT，fade_black转场
3. 创建 `food_vlog.json`：3个slot，暖色LUT，快节奏wipe转场
4. 创建 `workout.json`：3个slot，动感音乐，闪白转场
5. 创建 `pet_cute.json`：3个slot，俏皮贴纸，心形转场
6. 创建 `graduation.json`：4个slot，怀旧色调，交叉溶解
7. 创建 `city_night.json`：3个slot，冷色LUT，glitch转场
8. 创建 `family_reunion.json`：4个slot，暖色，柔和fade
9. 创建 `seasonal_autumn.json`：3个slot，橙色调，叶片转场
10. 创建 `music_dance.json`：4个slot，节拍同步，闪白快切
11. 在 `test_media_compat.cpp` 中批量加载所有模板，验证JSON格式正确

**完成标志**:
- [ ] 10个新模板JSON文件存在
- [ ] 每个模板可被 `TemplateEngine::loadFromString()` 解析
- [ ] 每个模板 `allSlotsFilled()` 后可生成有效Timeline

---

### Issue #14 — [P1] Clip: 实现时间重映射（变速曲线）框架

**目标**: 支持非线性速度变化。

**前置依赖**: 无

**涉及文件**:
- `core/include/timeline/Clip.h`
- `core/src/timeline/Clip.cpp`
- `tests/test_timeline.cpp`

**Jules 适配说明**: 纯数学计算，Jules 可以完成。

**步骤**:
1. 在 `Clip` 中新增 `std::map<int64_t, float> m_speedCurve`
2. 实现 `setSpeedCurvePoint(int64_t relativeTimeNs, float speed)`
3. 实现 `getSpeedAtTime(int64_t relativeTimeNs) -> float`：线性插值速度曲线
4. 实现 `getRemappedTime(int64_t timelineNs) -> int64_t`：对速度曲线积分，将时间线时间映射回源素材时间
5. 在 `test_timeline.cpp` 中测试：
   - 设置0.5x→1.0x→2.0x曲线，验证 `getRemappedTime()` 映射正确
   - 恒定速度1.0x时映射为恒等映射

**完成标志**:
- [ ] `setSpeedCurvePoint()` 正确添加曲线点
- [ ] `getSpeedAtTime()` 线性插值正确
- [ ] `getRemappedTime()` 积分映射正确
- [ ] 恒定速度1.0x时映射为恒等映射
- [ ] `test_timeline` 新增测试通过

---

## Phase 3: 体验优化（P2，部分有前置依赖）

---

### Issue #15 — [P2] BeautyFilter: 添加祛痘效果参数接口（CPU端）

**目标**: 在美颜滤镜中增加祛痘功能参数接口。

**前置依赖**: 无

**涉及文件**:
- `core/include/ai/BeautyFilter.h`
- `core/src/ai/BeautyFilter.cpp`
- `tests/test_ai_inference.cpp`

**Jules 适配说明**: 仅添加参数接口和CPU端逻辑，GLSL shader修改标记为人工检查点。

**步骤**:
1. 在 `BeautyFilter` 中新增参数 `acneRemovalStrength` [0,1]
2. 实现 `setParameter("acneRemovalStrength", value)` 的读取逻辑
3. 在 `getFragmentShaderSource()` 中预留 `u_acneRemovalStrength` uniform声明（注释标记）
4. 在 `test_ai_inference.cpp` 中测试参数set/get一致性

**完成标志**:
- [ ] `acneRemovalStrength` 参数可正确设置和读取
- [ ] shader源码中包含预留uniform声明
- [ ] `test_ai_inference` 新增测试通过

**人工检查点**: 实际的祛痘GLSL算法需要人工设计和验证。Jules 仅完成参数接口框架。

---

### Issue #16 — [P2] ParticleSystem: 完善重力与生命周期曲线

**目标**: 增强粒子系统的物理模拟能力。

**前置依赖**: 无

**涉及文件**:
- `core/include/ai/ParticleSystem.h`
- `core/src/ai/ParticleSystem.cpp`
- `tests/test_phase3_gaps.cpp`

**Jules 适配说明**: 纯物理模拟计算，Jules 可以完成。

**步骤**:
1. 确认粒子属性结构已包含：`velocity[2]`, `acceleration[2]` (重力), `lifeRemaining`, `initialLife`
2. 在 `update(float dt)` 中实现粒子更新：
   - `position += velocity * dt`
   - `velocity += acceleration * dt`
   - `lifeRemaining -= dt`
   - 移除 `lifeRemaining <= 0` 的粒子
3. 实现 `sizeOverLifetime`：粒子大小 = `initialSize * (lifeRemaining / initialLife)`
4. 实现 `colorOverLifetime`：alpha = `lifeRemaining / initialLife`（线性淡出）
5. 在 `test_phase3_gaps.cpp` 中测试：
   - 创建粒子系统，验证粒子受重力下落
   - 验证粒子随生命周期衰减大小和alpha
   - 验证死亡粒子被正确移除

**完成标志**:
- [ ] 粒子受重力影响下落
- [ ] 粒子随生命周期衰减大小和alpha
- [ ] 死亡粒子被正确移除
- [ ] 发射率稳定（每秒发射数恒定）
- [ ] `test_phase3_gaps` 新增测试通过

---

### Issue #17 — [P2] iOS: 创建 TimelineBridge.swift 基础桥接框架

**目标**: 补齐iOS端Timeline管理功能。

**前置依赖**: 无

**涉及文件**:
- 新增 `ios/Classes/TimelineBridge.swift`
- `ios/Classes/VideoSDK-Bridging-Header.h`

**Jules 适配说明**: Swift桥接代码，Jules 可以编写。但**需要Xcode环境验证编译**。

**步骤**:
1. 创建 `TimelineBridge.swift`，定义 `TimelineManager` 类
2. 实现 `createTimeline(width:height:fps:) -> Int64`：调用C++ `Timeline` 构造函数，返回native handle
3. 实现 `addTrack(handle:zIndex:type:)`：调用C++ `Timeline::addTrack()`
4. 实现 `addClip(handle:trackZIndex:clipId:sourcePath:)`：调用C++ `Track::addClip()`
5. 在 `VideoSDK-Bridging-Header.h` 中暴露对应C++接口

**完成标志**:
- [ ] `TimelineManager.createTimeline()` 返回有效handle
- [ ] `addTrack()` 成功添加轨道
- [ ] `addClip()` 成功添加片段
- [ ] 代码结构符合现有iOS桥接模式

**人工检查点**: 需要在macOS/Xcode环境中验证编译和运行。

---

### Issue #18 — [P2] iOS: 创建 ExportBridge.swift 导出桥接框架

**目标**: 补齐iOS端视频导出功能。

**前置依赖**: #17

**涉及文件**:
- 新增 `ios/Classes/ExportBridge.swift`
- `core/src/timeline/TimelineExporterIOS.mm`

**Jules 适配说明**: Swift桥接代码，Jules 可以编写。但**需要Xcode环境验证**。

**步骤**:
1. 创建 `ExportBridge.swift`，定义 `VideoExporter` 类
2. 实现 `export(timelineHandle:outputPath:width:height:fps:bitrate:)`：调用C++ `TimelineExporterIOS::exportAsync()`
3. 实现进度回调：通过Swift闭包转发C++进度
4. 在 `TimelineExporterIOS.mm` 中完善 `exportAsync()` 声明（如果尚未实现）
5. 实现 `cancel()` 方法声明

**完成标志**:
- [ ] `VideoExporter.export()` 启动导出任务
- [ ] 进度回调正确传递（0.0→1.0）
- [ ] `cancel()` 可中断导出
- [ ] 代码结构符合现有iOS桥接模式

**人工检查点**: 需要在macOS/Xcode环境中验证编译和运行。

---

### Issue #19 — [P2] EffectScriptEngine: 创建 Lua VM 管理器框架

**目标**: 创建独立的Lua脚本引擎，管理VM生命周期。

**前置依赖**: 无

**涉及文件**:
- 新增 `core/include/EffectScriptEngine.h`
- 新增 `core/src/EffectScriptEngine.cpp`
- `CMakeLists.txt`

**Jules 适配说明**: 需要集成Lua 5.4。Jules 可以编写C++封装代码，但**需要人工提供Lua源码**。

**步骤**:
1. 在 `CMakeLists.txt` 中添加Lua库查找逻辑（`find_package(lua54)` 或 `find_library`）
2. 创建 `EffectScriptEngine` 类：成员 `lua_State* L`
3. 实现构造函数：调用 `luaL_newstate()` + `luaL_openlibs()`
4. 实现 `loadScript(const std::string& source)`: 调用 `luaL_dostring(L, source.c_str())`，出错时返回错误信息
5. 实现 `callOnFrame(int64_t timestampNs)`: 将 `timestampNs` 压栈，调用 Lua 函数 `onFrame(ts)`
6. 实现析构函数调用 `lua_close(L)`
7. 在 `test_phase3_gaps.cpp` 中添加测试：
   - 加载简单Lua脚本 `function onFrame(ts) return ts end`
   - 验证 `callOnFrame(1000)` 不崩溃

**完成标志**:
- [ ] `loadScript()` 成功加载有效Lua脚本
- [ ] `loadScript()` 对语法错误脚本返回明确错误信息
- [ ] `callOnFrame()` 正确传递时间戳参数
- [ ] 析构函数释放Lua VM无泄漏
- [ ] `test_phase3_gaps` 新增测试通过

**人工检查点**: 需要人工将Lua 5.4源码放入 `third_party/lua/` 或确保系统已安装Lua开发库。

---

## 依赖关系图

```
Phase 1 (P0):
  #1 FFmpeg error handling + tests
    ↓
  #2 DecoderPool soft fallback

  #3 FaceLandmark decodeLandmarks
  #4 BodyPose decodeKeypoints
  #5 SegmentationFilter params
  #6 Bezier keyframes
  #7 Transitions batch 1 (dissolve/pixelate/circle_crop)
  #8 EFFECT track + EffectClip

Phase 2 (P1):
  #7 ↓
  #9 Transitions batch 2 (glitch/rotate_in/radial_wipe)
  #10 EQ Biquad framework
  #11 MergeNode
  #12 ExportRecovery
  #13 Templates x10
  #14 Time remapping

Phase 3 (P2):
  #15 BeautyFilter acne params
  #16 Particle physics
  #17 iOS TimelineBridge
    ↓
  #18 iOS ExportBridge
  #19 Lua VM manager
```

## 使用方式

1. 将仓库推送到GitHub
2. 在 jules.google.com 连接仓库（Jules基于 Gemini 3.1 Pro）
3. 按Phase顺序创建Issue（Phase 1优先，有依赖关系的等前置Issue合并后再创建）
4. 对每个Issue添加标签：
   - `assign-to-jules`：Jules可以独立完成
   - `jules-with-human-review`：需要人工验证（GLSL/iOS/音频等）
   - `needs-external-resource`：需要人工提供外部资源（.tflite/Lua源码等）
5. Jules会自动制定计划、执行修改、提交PR
6. 审核PR后合并

## 人工检查点汇总

| Issue | 人工检查内容 | 原因 |
|-------|-------------|------|
| #7, #9 | GLSL shader视觉效果 | Jules无法判断视觉质量 |
| #10 | 音频EQ听感 | Jules无法判断音质 |
| #15 | 祛痘算法效果 | 需要人工设计GLSL |
| #17, #18 | iOS编译和运行 | 需要Xcode/macOS环境 |
| #19 | Lua源码/库 | 需要人工提供或安装 |
| #3, #4, #5 | AI模型集成 | 需要真实.tflite文件验证 |

## 不适合Jules的任务（需人工完成）

以下任务**不建议**分配给Jules：

1. **从零设计复杂GLSL shader**（如高级美颜算法、物理仿真shader）
2. **音频算法调参**（如混响参数、EQ曲线微调）
3. **性能基准测试**（需要真实设备运行）
4. **iOS真机调试**（需要Apple Developer账号和物理设备）
5. **AI模型训练/转换**（需要GPU资源和专业知识）
6. **跨平台兼容性验证**（需要多设备测试）