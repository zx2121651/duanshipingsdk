#!/usr/bin/env pwsh
<#
.SYNOPSIS
    One-shot FFmpeg prebuilt downloader for Android.
.DESCRIPTION
    Sources:
      .so files  — ffmpeg-kit-full 6.0-2.LTS AAR from Maven Central
                   (jni/<abi>/libavcodec.so  libavformat.so  libavutil.so  libswscale.so)
      Headers    — FFmpeg 6.0 source tarball from ffmpeg.org
                   (libavcodec/*.h → include/libavcodec/*.h, etc.)

    Expected layout after this script:
        android/src/main/jniLibs/ffmpeg/include/libavcodec/avcodec.h
        android/src/main/jniLibs/ffmpeg/include/libavformat/avformat.h
        android/src/main/jniLibs/ffmpeg/include/libavutil/imgutils.h
        android/src/main/jniLibs/ffmpeg/include/libswscale/swscale.h
        android/src/main/jniLibs/ffmpeg/arm64-v8a/libavcodec.so  ...
        android/src/main/jniLibs/ffmpeg/armeabi-v7a/libavcodec.so ...
        android/src/main/jniLibs/ffmpeg/x86/libavcodec.so         ...
        android/src/main/jniLibs/ffmpeg/x86_64/libavcodec.so      ...

    After completion local.properties is updated with ffmpegPrebuiltDir so
    Gradle passes -DFFMPEG_PREBUILT_DIR to CMake automatically.

.PARAMETER OutDir
    Override extraction target. Default: android/src/main/jniLibs/ffmpeg

.EXAMPLE
    .\scripts\download_ffmpeg_android.ps1
    .\scripts\download_ffmpeg_android.ps1 -OutDir "D:\libs\ffmpeg"
#>

param(
    [string]$OutDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot "android\src\main\jniLibs\ffmpeg"
}
$TmpDir = Join-Path $env:TEMP "ffmpeg-android-dl"
New-Item -ItemType Directory -Force -Path $TmpDir  | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir  | Out-Null

$ProgressPreference = "SilentlyContinue"

function Download-File([string]$Url, [string]$Dest) {
    if (Test-Path $Dest) {
        Write-Host "  [cached] $(Split-Path $Dest -Leaf)"
        return
    }
    Write-Host "  Downloading $(Split-Path $Dest -Leaf) ..."
    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
}

# ---------------------------------------------------------------------------
# Delegate to build_ffmpeg_android.sh via Git Bash
# (builds FFmpeg 6.0 from source using NDK make.exe + Clang toolchain)
# ---------------------------------------------------------------------------
$GitBash = @(
    "C:\Program Files\Git\bin\bash.exe",
    "C:\Program Files\Git\usr\bin\bash.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $GitBash) {
    Write-Error "Git Bash not found. Install Git for Windows from https://git-scm.com"
}

$BuildScript = Join-Path $RepoRoot "scripts\build_ffmpeg_android.sh"
# Convert Windows path to Unix for bash
$BuildScriptUnix = $BuildScript -replace '\\', '/' -replace '^([A-Za-z]):', { '/' + $matches[1].ToLower() }

Write-Host ""
Write-Host "=== Building FFmpeg 6.0 from source via NDK ==="
Write-Host "  Script: $BuildScript"
Write-Host "  This will take 5-15 minutes on first run ..."
Write-Host ""

& $GitBash -c "bash '$BuildScriptUnix'"
if ($LASTEXITCODE -ne 0) {
    Write-Error "build_ffmpeg_android.sh failed (exit $LASTEXITCODE)"
}

# ---------------------------------------------------------------------------
# Verify sentinel (local.properties updated by the .sh script)
# ---------------------------------------------------------------------------
$Sentinel = Join-Path $OutDir "include\libavcodec\avcodec.h"
if (-not (Test-Path $Sentinel)) {
    Write-Error "Sentinel not found after build: $Sentinel"
}
Write-Host "  Sentinel OK: include/libavcodec/avcodec.h"

Write-Host ""
Write-Host "=== All done! ==="
Write-Host "  Run:  .\gradlew :android:assembleDebug"
Write-Host "  CMake detects FFMPEG_PREBUILT_DIR and compiles HAS_FFMPEG_DECODER."
Write-Host "  FFmpegVideoDecoder (YUV I420/NV12 → RGB) will be active."
