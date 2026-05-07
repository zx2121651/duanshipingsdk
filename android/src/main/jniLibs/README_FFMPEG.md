# FFmpeg prebuilt layout

FFmpeg software decoding is optional. The Android build enables `HAS_FFMPEG_DECODER` only when the required headers and per-ABI shared libraries are present.

## 一键脚本（推荐）

### Windows (PowerShell)
```powershell
# 从项目根目录执行（需要网络访问 GitHub Releases）
.\scripts\download_ffmpeg_android.ps1

# 指定版本或自定义输出目录
.\scripts\download_ffmpeg_android.ps1 -Version "6.0" -OutDir "D:\libs\ffmpeg"
```

### Linux / macOS (Bash)
```bash
chmod +x scripts/download_ffmpeg_android.sh
./scripts/download_ffmpeg_android.sh            # 默认 v6.0
./scripts/download_ffmpeg_android.sh 5.1        # 指定版本
```

两个脚本会自动将 `ffmpegPrebuiltDir=<path>` 写入 `local.properties`，
再次执行 `./gradlew :android:assembleDebug` 时 CMake 变量 `FFMPEG_PREBUILT_DIR`
会被自动传入，编译器宏 `HAS_FFMPEG_DECODER` 被激活，`SoftwareVideoDecoder`
改用 FFmpeg `libavcodec` 后端，`DecoderFallbackStrategy.AUTO/SW_FIRST` 生效。

## 手动安装（回退路径）

如果脚本因网络原因无法运行，可按以下步骤手动操作：

1. 前往 https://github.com/arthenica/ffmpeg-kit/releases 下载
   `ffmpeg-kit-full-6.0-android-<abi>.zip`（arm64-v8a 和 armeabi-v7a）
2. 将内容解压到本文档所在目录，或任意自定义路径
3. 在 `local.properties` 中添加一行：
   ```properties
   ffmpegPrebuiltDir=/your/custom/path
   ```

## Default location

Place prebuilt files under:

```text
android/src/main/jniLibs/ffmpeg/
  include/
    libavcodec/avcodec.h
    libavformat/avformat.h
    libavutil/...
    libswscale/...
  arm64-v8a/
    libavcodec.so
    libavformat.so
    libavutil.so
    libswscale.so
  armeabi-v7a/
    libavcodec.so
    libavformat.so
    libavutil.so
    libswscale.so
```

## Custom location

You can pass a custom directory without editing `build.gradle`:

```powershell
.\gradlew.bat :android:assembleDebug -PffmpegPrebuiltDir=C:/path/to/ffmpeg
```

If the directory is missing or incomplete, the build keeps using the `SoftwareVideoDecoder` stub and `TimelineManager.isSoftwareDecoderAvailable()` returns `false`.
