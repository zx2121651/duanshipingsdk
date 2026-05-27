# ShortVideoSDK

ShortVideoSDK 是一个高性能、跨平台的短视频编辑与特效 SDK 源码仓库。其核心引擎层由 C++ 编写，保证多端渲染与算法运行的一致性与高效率；Android 平台通过 Kotlin/JNI 封装为 AAR 库形式，iOS 平台则通过 Swift 与 Objective-C++ 桥接层供宿主 App 接入。

该项目面向 SDK 的二次开发与编译打包。对外交付时，建议使用项目自带的发布打包脚本生成干净的发布包。

---

## 架构概览

```text
┌─────────────────────────────────────────────────────────┐
│                   Platform Layer                         │
│  ┌──────────────────┐    ┌──────────────────────────┐   │
│  │  Android (AAR)   │    │  iOS (Framework)         │   │
│  │  Kotlin/JNI      │    │  Swift/ObjC++ Bridge     │   │
│  └──────────────────┘    └──────────────────────────┘   │
├─────────────────────────────────────────────────────────┤
│                   Core C++ Engine                        │
│  ┌──────┐ ┌──────┐ ┌──────────┐ ┌───────┐ ┌─────────┐  │
│  │  RHI │ │  AI  │ │ Timeline │ │Filter │ │ Pipeline│  │
│  │GL/Vk │ │TFLite│ │  NLE     │ │Engine │ │  Graph  │  │
│  │Metal │ │      │ │          │ │       │ │         │  │
│  └──────┘ ┌──────┐ ┌──────────┐ ┌───────┐ ┌─────────┐  │
│           │Audio │ │ Exporter │ │  FBO  │ │ DSR/Mem │  │
│           │      │ │          │ │ Pool  │ │ Mgmt    │  │
│           └──────┘ └──────────┘ ┌───────┘ ┌─────────┐  │
│                                    │Context│ │ Perf   │  │
│                                    │Recovery│Metrics │  │
│                                    └───────┘ └─────────┘  │
├─────────────────────────────────────────────────────────┤
│                   Assets & Resources                     │
│  Shaders (GLSL/Metal) │ Templates (JSON) │ AI Models   │
└─────────────────────────────────────────────────────────┘
```

---

## 目录结构

```text
.
├── sdk-core/               # 跨平台 C++ 核心引擎
│   ├── core/               # 核心源码
│   │   ├── include/        # 核心公共头文件 (ai/, rhi/, timeline/, pipeline/ 等)
│   │   └── src/            # 核心实现源码
│   ├── mock_gl/            # Mock GLES (无 GPU 环境下的头文件替代，用于测试与 Linux CI)
│   ├── tests/              # C++ 单元测试 (包含完整单元测试用例)
│   ├── shaders/            # SPIR-V 编译及着色器管理相关
│   ├── scripts/            # 构建辅助脚本
│   └── CMakeLists.txt      # CMake 构建配置
├── android/                # Android 原生封装项目 (Gradle 工程根目录)
│   ├── android/            # SDK 原生 Android 库 (输出 AAR，含 JNI Bridge)
│   ├── core-android/       # Android 专属底层封装模块 (UI, Model)
│   ├── features/           # Android Demo 功能模块 (Capture, Editor 等)
│   ├── samples/android/    # Android 演示与集成测试宿主 App (Demo)
│   └── settings.gradle     # Gradle 多模块项目配置
├── ios/                    # iOS 平台封装层
│   ├── Classes/            # Swift 与 Objective-C++ 桥接源码
│   ├── VideoSDK.xcodeproj  # Xcode 工程项目
│   ├── VideoSDK.podspec    # CocoaPods 描述文件
│   └── Package.swift       # Swift Package Manager (SPM) 描述文件
├── assets/                 # 平台公用滤镜、特效及模板资源
│   ├── shaders/            # GLSL / Metal 着色器 (包含 msl/ 子目录)
│   └── templates/          # 视频模板 JSON 配置
├── docs/                   # 集成与设计规范文档
│   ├── android-integration.md
│   ├── ios-integration.md
│   ├── dependencies.md
│   └── release-package.md
└── tools/                  # 发布打包工具脚本
    └── package-release.ps1 # 一键发布包生成脚本 (PowerShell)
```

---

## 核心功能

*   **高性能渲染引擎**：基于 RHI（Render Hardware Interface）抽象层，支持 OpenGL ES 3.0+、Vulkan 与 Metal 多渲染后端。引擎在运行时自动探测硬件能力，并遵循 `Metal → Vulkan → GLES` 链条进行优雅降级。
*   **智能 AI 特效**：支持实时美颜、瘦脸大眼、人手/人脸关键点检测、人像/头发分割、手势识别及绿幕抠图等。推理引擎基于 TensorFlow Lite，支持 `GPU → NNAPI → XNNPACK → CPU` 四级推理降级链，兼顾性能与兼容性。
*   **非线性时间线编辑 (NLE)**：支持多轨道编辑、音频混音、变声特效、智能裁剪、转场动画、关键帧插值（支持贝塞尔曲线）、时间重映射以及草稿自动保存等丰富功能。
*   **多格式视频导出**：集成高性能软硬解码（含 FFmpeg 软解与平台硬解），支持 HEVC 硬编探测、VBR 动态码率控制、多分辨率、多帧率的视频导出。
*   **动态分辨率缩放 (DSR)**：基于 GPU 计时的自适应帧率保障机制，在 GPU 超载时自动降低渲染分辨率以确保流畅度，在负载空闲时自动恢复质量。

---

## 健壮性与防御性设计

*   **GL Context 丢失恢复**：完整实现了 `onContextLost` / `onContextRestored` 逻辑，完美处理移动端后台切换或系统驱动驱逐导致的 GPU 上下文丢失重建。
*   **内存压力响应 (Trim Memory)**：提供五档 VRAM 预算管理机制，与 Android `ComponentCallbacks2.onTrimMemory` 对齐，在内存紧张时自动释放纹理和 FBO 缓存以防闪退。
*   **线程安全校验**：通过核心层 `ThreadCheck` 工具绑定渲染线程，在所有 GPU 敏感操作前进行强制校验，避免多线程并发引起的死锁或渲染崩溃。
*   **运行时 RHI 后端覆盖**：支持通过 `SDK_RHI_BACKEND` 环境变量或 `debug.sdk.rhi.backend` 系统属性强制指定渲染后端，便于开发者进行跨平台兼容性调试。

---

## 构建与运行指南

### 1. C++ 核心引擎 (`sdk-core`)

核心层使用 CMake 构建，支持按需编译。

#### 编译先决条件
*   CMake 3.10.2+
*   支持 C++17 的编译器 (MSVC 2019+ / Clang / GCC)
*   （可选）FFmpeg 预编译包（启用 `-DWITH_FFMPEG=ON` 以支持软解）
*   （可选）TensorFlow Lite 库（启用 `-DWITH_TFLITE=ON` 以支持 AI 特效）
*   （可选）Vulkan SDK（启用 `-DWITH_VULKAN=ON` 以支持 Vulkan 后端）

#### Windows — Mock GL 构建 (无 GPU 依赖，最快，适合跑单元测试)
```bash
cd sdk-core
mkdir build && cd build
cmake .. -DUSE_MOCK_GL=ON
cmake --build . --config Release
ctest -C Release
```

#### Windows — ANGLE 构建 (基于真实 GPU 的 GLES 渲染)
```powershell
cd sdk-core
.\setup_windows_env.ps1    # 自动下载配置 vcpkg 与 ANGLE 并初始化 CMake
cd build
cmake --build . --config Release
ctest -C Release
```
或者也可以直接运行快捷批处理脚本：
```bash
cd sdk-core
setup_win.bat
```

#### Linux 构建
```bash
cd sdk-core
mkdir build && cd build
cmake .. -DUSE_MOCK_GL=ON   # 若本地已配置 GLES 驱动，可去掉该参数
cmake --build . --config Release
ctest
```

---

### 2. Android 端

#### 构建要求
*   JDK 17
*   Android SDK 34
*   Android NDK 25.1.8937393
*   CMake 3.22.1 (使用 SDK Manager 安装)
*   Android Gradle Plugin (AGP) 8.2.1

#### 构建 AAR
```bash
cd android
./gradlew :android:assembleRelease
```
构建成功后输出文件位于：`android/android/build/outputs/aar/android-release.aar`

#### 运行 Demo App
```bash
cd android
./gradlew :sample-android:installDebug
```
*注：需连接支持 API 24+ 的 Android 真机或模拟器。*

#### 可选：FFmpeg 软解支持
在 `android/local.properties` 文件中，指定你的预编译 FFmpeg 库路径：
```properties
ffmpegPrebuiltDir=/path/to/ffmpeg/prebuilt
```
关于 Android 集成的更多细节，请参见 [docs/android-integration.md](docs/android-integration.md)。

---

### 3. iOS 端

#### 构建要求
*   macOS 平台
*   Xcode 14+
*   Swift 5.9+

#### CocoaPods 集成
在宿主项目的 `Podfile` 中添加本地路径依赖：
```ruby
pod 'VideoSDK', :path => '../ios'
```

#### SPM (Swift Package Manager) 集成
在 Xcode 中选择 `File` → `Add Package Dependencies`，并将本地的 `ios/` 文件夹拖入或导入其中。

#### 直接打开 Xcode 工程调试
```bash
open ios/VideoSDK.xcodeproj
```
关于 iOS 集成的更多细节，请参见 [docs/ios-integration.md](docs/ios-integration.md)。

---

### 4. 发布打包

SDK 提供了快速发布脚本，可以一键打包出干净的交付包。

```powershell
.\tools\package-release.ps1 -Version 1.0.0
```

如果 Android AAR 库此前已经编译完成，可以使用以下命令跳过构建过程：
```powershell
.\tools\package-release.ps1 -Version 1.0.0 -SkipBuild
```

#### 打包输出产物
打包完成后，将在根目录的 `dist/` 文件夹下生成：
*   `dist/ShortVideoSDK-1.0.0/`：干净的发布源码与库目录。
*   `dist/ShortVideoSDK-1.0.0.zip`：压缩包文件。

关于打包后的文件结构，请参见 [docs/release-package.md](docs/release-package.md)。

---

## 环境要求汇总

| 平台 | 构建要求 | 核心工具 / 依赖 |
|------|------|----------------|
| **C++ Core** | CMake 3.10.2+, C++17 | MSVC 2019+ / Clang / GCC |
| **Android** | SDK 34, NDK 25.1.8937393, minSdk 24 | JDK 17, CMake 3.22.1, AGP 8.2.1 |
| **iOS** | iOS 12.0+ | Xcode 14+, Swift 5.9+ (Swift 兼容 5.0+) |

---

## 许可证

本项目基于 **Apache License 2.0** 许可证开源，详情参见 [LICENSE](LICENSE) 文件。