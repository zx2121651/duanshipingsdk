# Logcat 诊断 & 修复报告 (2026-06-02) — 最终版

## 修改文件 (4个)

| # | 文件 | 修改内容 |
|---|------|---------|
| 1 | `RenderEngine.kt` | post-drain 移至 listener 回调之后（防止 onFrameOutput GL 渲染残留 0x502） |
| 2 | `FilterEngine.cpp` | `m_graph->execute()` 后增加 post-exec GL error drain |
| 3 | `FilterEngine.h` + `FilterEngine.cpp` | **添加 `m_pendingMutex` + 锁保护 `m_pendingFrames`/`m_pendingFrameIds` 并发访问** |
| 4 | `proguard-rules.pro` | 添加 Compose snapshots + coroutines keep 规则 |

## 问题根因链

```
问题1: GL error 0x502 每帧洪水
├─ C++ execute() 产生 0x502
├─ C++ drain 清空 (V2 fix)
├─ 但 onFrameOutput listener 做 GL 渲染产生新 0x502
└─ 移至 listener 之后 drain (V3 fix) → 应彻底消失

问题2: 进程启动 2-3 秒后 SIGSEGV crash
├─ processFrame() (GL线程) 写 m_pendingFrames / m_pendingFrameIds
├─ markFrameRendered() (SurfaceView渲染器线程) 并发读/写同一容器
└─ 无锁 → std::vector/unordered_map 并发修改 → 堆损坏 → SIGSEGV
```

## 验证步骤

```bash
cd android/android && ./gradlew assembleDebug && adb install -r app/build/outputs/apk/debug/app-debug.apk

# 验证 0x502 消失
adb logcat -s RenderEngine:E FilterEngine:D | grep "0x502"

# 验证 crash 消失
adb logcat -s DEBUG:V | grep -E "SIGSEGV|Fatal signal|tombstone"
```
