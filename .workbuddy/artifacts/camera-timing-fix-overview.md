# CameraX 时序死锁修复

## 问题

`MainActivity.startCamera()` 在 `setContent(HomeScaffold)` 之前调用，但：
- 默认路由是 `"home"`，不是 `"capture"`
- `FilterCameraPreview` 仅在 `CaptureScreen` 中存在
- CameraX `SurfaceProvider` 调用 `awaitInputSurface()` 等待 `engineState == RUNNING`
- 引擎只能由 `FilterCameraPreview.onSurfaceCreated()` 初始化
- 首页根本没有预览组件 → 5 秒超时 → `Cannot complete surfaceList within 5000`

## 修复方案

**解耦 VideoFilterManager 创建与 CameraX 绑定**，通过 ViewModel 回调驱动生命周���。

### 新事件流

```
用户进入拍摄页
  → DisposableEffect → enterCapture()
    → startCaptureSession() → 创建 VideoFilterManager
      → FilterCameraPreview 组合 → onSurfaceCreated → initialize()
        → engineState = RUNNING
          → LaunchedEffect(engineState) → requestCameraBind()
            → bindCameraToPreview() → CameraX bindToLifecycle
              → SurfaceProvider 触发 → awaitInputSurface() 立即返回 ✓

用户离开拍摄页
  → DisposableEffect.onDispose → leaveCapture()
    → endCaptureSession() → unbindCamera()
```

### 修改文件

| 文件 | 改动 |
|------|------|
| `AppViewModel.kt` | 新增 `enterCapture()` / `leaveCapture()` / `requestCameraBind()` 回调 |
| `MainActivity.kt` | 移除 `onCreate` 中的 `startCamera()`，拆分为 `startCaptureSession()` + `bindCameraToPreview()` + `endCaptureSession()` |
| `CaptureScreen.kt` | 新增 `DisposableEffect` 管理会话生命周期 + `LaunchedEffect(engineState)` 触发相机绑定 |
