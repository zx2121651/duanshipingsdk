#!/usr/bin/env bash
# build_ffmpeg_android.sh — Build FFmpeg 6.0 for Android using NDK
#
# Run from repo root under Git Bash (Windows):
#   bash scripts/build_ffmpeg_android.sh
#
# Requirements:
#   - Android NDK (auto-detected from %LOCALAPPDATA%/Android/Sdk/ndk/)
#   - Git Bash on Windows, or bash on Linux/macOS
#
# Output:
#   android/src/main/jniLibs/ffmpeg/include/libavcodec/avcodec.h  (headers)
#   android/src/main/jniLibs/ffmpeg/arm64-v8a/libavcodec.so       (arm64 libs)
#   android/src/main/jniLibs/ffmpeg/armeabi-v7a/libavcodec.so     (arm32 libs)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FFMPEG_VERSION="6.0"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
OUT_BASE="$REPO_ROOT/android/src/main/jniLibs/ffmpeg"
TMP_DIR="/tmp/ffmpeg-build-android"
API_LEVEL=24
ABIS=("arm64-v8a" "armeabi-v7a")

mkdir -p "$TMP_DIR" "$OUT_BASE"

# ---------------------------------------------------------------------------
# Locate NDK
# ---------------------------------------------------------------------------
find_ndk() {
    # Windows path via Git Bash
    local appdata_win="${LOCALAPPDATA:-}"
    if [[ -n "$appdata_win" ]]; then
        local appdata_bash
        appdata_bash="$(cygpath -u "$appdata_win" 2>/dev/null || echo "$appdata_win" | sed 's|\\|/|g' | sed 's|C:|/c|g')"
        local ndk_root="$appdata_bash/Android/Sdk/ndk"
        if [[ -d "$ndk_root" ]]; then
            # prefer 25.x for stability
            for v in 25.1.8937393 25.2.9519653 26.1.10909125; do
                [[ -d "$ndk_root/$v" ]] && echo "$ndk_root/$v" && return 0
            done
            # fallback: first available
            ls -d "$ndk_root"/*/  2>/dev/null | head -1 | tr -d '/' && return 0
        fi
    fi
    # Linux/macOS
    for d in "$HOME/Android/Sdk/ndk/"*; do
        [[ -d "$d" ]] && echo "$d" && return 0
    done
    echo ""
}

NDK_PATH="$(find_ndk)"
if [[ -z "$NDK_PATH" ]]; then
    echo "ERROR: Android NDK not found. Set ANDROID_NDK_HOME or install via Android Studio." >&2
    exit 1
fi
echo "NDK: $NDK_PATH"

# Determine host tag
if [[ "$(uname -s)" == MINGW* ]] || [[ "$(uname -s)" == MSYS* ]] || [[ -n "${LOCALAPPDATA:-}" ]]; then
    HOST_TAG="windows-x86_64"
    MAKE_EXE="$NDK_PATH/prebuilt/windows-x86_64/bin/make.exe"
elif [[ "$(uname -s)" == Darwin ]]; then
    HOST_TAG="darwin-x86_64"
    MAKE_EXE="make"
else
    HOST_TAG="linux-x86_64"
    MAKE_EXE="make"
fi
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"
echo "HOST_TAG: $HOST_TAG"
echo "MAKE: $MAKE_EXE"

# ---------------------------------------------------------------------------
# Download FFmpeg source (cached)
# ---------------------------------------------------------------------------
TAR_PATH="$TMP_DIR/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
SRC_DIR="$TMP_DIR/ffmpeg-${FFMPEG_VERSION}"

if [[ ! -f "$TAR_PATH" ]]; then
    echo "Downloading FFmpeg ${FFMPEG_VERSION} ..."
    curl -fL --progress-bar -o "$TAR_PATH" "$FFMPEG_URL"
fi
if [[ ! -d "$SRC_DIR" ]]; then
    echo "Extracting FFmpeg source ..."
    tar -xjf "$TAR_PATH" -C "$TMP_DIR"
fi

# Copy headers to output (done once, ABI-independent)
INCLUDE_OUT="$OUT_BASE/include"
if [[ ! -f "$INCLUDE_OUT/libavcodec/avcodec.h" ]]; then
    echo "Installing headers ..."
    for lib in libavcodec libavformat libavutil libswscale; do
        mkdir -p "$INCLUDE_OUT/$lib"
        cp "$SRC_DIR/$lib/"*.h "$INCLUDE_OUT/$lib/" 2>/dev/null || true
    done
    # Generate avconfig.h + ffversion.h (produced by build system)
    cat > "$INCLUDE_OUT/libavutil/avconfig.h" << 'EOF'
/* Android (little-endian) */
#ifndef AVUTIL_AVCONFIG_H
#define AVUTIL_AVCONFIG_H
#define AV_HAVE_BIGENDIAN 0
#define AV_HAVE_FAST_UNALIGNED 1
#endif
EOF
    cat > "$INCLUDE_OUT/libavutil/ffversion.h" << EOF
#ifndef AVUTIL_FFVERSION_H
#define AVUTIL_FFVERSION_H
#define FFMPEG_VERSION "${FFMPEG_VERSION}"
#endif
EOF
fi

# ---------------------------------------------------------------------------
# Build helper
# ---------------------------------------------------------------------------
build_abi() {
    local ABI="$1"
    local ABI_DIR="$OUT_BASE/$ABI"
    mkdir -p "$ABI_DIR"

    if [[ -f "$ABI_DIR/libavcodec.so" ]]; then
        echo "  [$ABI] already built, skipping."
        return 0
    fi

    local INSTALL_DIR="$TMP_DIR/install-$ABI"
    mkdir -p "$INSTALL_DIR"

    local ARCH TRIPLE CPU EXTRA_CFLAGS
    case "$ABI" in
        arm64-v8a)
            ARCH="aarch64"; TRIPLE="aarch64-linux-android"; CPU="armv8-a"
            EXTRA_CFLAGS=""
            ;;
        armeabi-v7a)
            ARCH="arm"; TRIPLE="armv7a-linux-androideabi"; CPU="armv7-a"
            EXTRA_CFLAGS="-mfpu=neon -mfloat-abi=softfp"
            ;;
        x86)
            ARCH="x86"; TRIPLE="i686-linux-android"; CPU="i686"
            EXTRA_CFLAGS=""
            ;;
        x86_64)
            ARCH="x86_64"; TRIPLE="x86_64-linux-android"; CPU="x86-64"
            EXTRA_CFLAGS=""
            ;;
    esac

    local CC="$TOOLCHAIN/bin/${TRIPLE}${API_LEVEL}-clang"
    local CXX="$TOOLCHAIN/bin/${TRIPLE}${API_LEVEL}-clang++"
    # Windows NDK wraps clang as .cmd files — detect both
    [[ ! -f "$CC" && -f "${CC}.cmd" ]] && CC="${CC}.cmd"
    [[ ! -f "$CXX" && -f "${CXX}.cmd" ]] && CXX="${CXX}.cmd"

    local AR="$TOOLCHAIN/bin/llvm-ar"
    local RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    local STRIP="$TOOLCHAIN/bin/llvm-strip"

    echo ""
    echo "=== Building $ABI ==="
    echo "  CC=$CC"

    # FFmpeg requires in-tree build — run configure from source directory
    pushd "$SRC_DIR" > /dev/null

    # Clean any prior configuration for previous ABI
    if [[ -f "Makefile" ]]; then
        "$MAKE_EXE" distclean 2>/dev/null || true
    fi

    ./configure \
        --prefix="$INSTALL_DIR" \
        --target-os=android \
        --arch="$ARCH" \
        --cpu="$CPU" \
        --cc="$CC" \
        --cxx="$CXX" \
        --ld="$CC" \
        --ar="$AR" \
        --ranlib="$RANLIB" \
        --strip="$STRIP" \
        --nm="$TOOLCHAIN/bin/llvm-nm" \
        --enable-cross-compile \
        --sysroot="$TOOLCHAIN/sysroot" \
        --disable-static \
        --enable-shared \
        --disable-programs \
        --disable-doc \
        --disable-htmlpages \
        --disable-manpages \
        --disable-podpages \
        --disable-txtpages \
        --disable-avdevice \
        --disable-postproc \
        --disable-avfilter \
        --disable-network \
        --disable-bsfs \
        --disable-indevs \
        --disable-outdevs \
        --disable-devices \
        --disable-everything \
        --disable-vulkan \
        --disable-hwaccels \
        --enable-swscale \
        --enable-swresample \
        --enable-decoder=h264 \
        --enable-decoder=hevc \
        --enable-decoder=vp8 \
        --enable-decoder=vp9 \
        --enable-decoder=mpeg4 \
        --enable-decoder=aac \
        --enable-decoder=mp3 \
        --enable-demuxer=mov \
        --enable-demuxer=matroska \
        --enable-demuxer=avi \
        --enable-demuxer=flv \
        --enable-parser=h264 \
        --enable-parser=hevc \
        --enable-parser=mpeg4video \
        --enable-parser=aac \
        --enable-protocol=file \
        --extra-cflags="-DANDROID -fPIC -ffunction-sections -fdata-sections ${EXTRA_CFLAGS}" \
        --extra-ldflags="-Wl,--gc-sections" \
        2>&1 | tail -5

    "$MAKE_EXE" -j2 2>&1 | tail -3
    "$MAKE_EXE" install 2>&1 | tail -2

    popd > /dev/null

    # Copy .so files to ABI output dir
    cp "$INSTALL_DIR/lib/"*.so "$ABI_DIR/" 2>/dev/null || true
    echo "  $ABI: $(ls "$ABI_DIR/"*.so 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
}

# ---------------------------------------------------------------------------
# Build each ABI
# ---------------------------------------------------------------------------
for abi in "${ABIS[@]}"; do
    build_abi "$abi"
done

# ---------------------------------------------------------------------------
# Verify sentinel
# ---------------------------------------------------------------------------
SENTINEL="$OUT_BASE/include/libavcodec/avcodec.h"
[[ ! -f "$SENTINEL" ]] && { echo "ERROR: Sentinel not found: $SENTINEL"; exit 1; }
ARM64_SO="$OUT_BASE/arm64-v8a/libavcodec.so"
[[ ! -f "$ARM64_SO" ]] && { echo "ERROR: arm64-v8a/libavcodec.so not found"; exit 1; }
echo ""
echo "=== Build complete ==="
echo "  Headers : $OUT_BASE/include/"
echo "  arm64   : $(ls "$OUT_BASE/arm64-v8a/"*.so | wc -l) .so files"
echo "  arm32   : $(ls "$OUT_BASE/armeabi-v7a/"*.so | wc -l) .so files"

# ---------------------------------------------------------------------------
# Update local.properties
# ---------------------------------------------------------------------------
LOCAL_PROPS="$REPO_ROOT/local.properties"
PROP_LINE="ffmpegPrebuiltDir=${OUT_BASE}"
if [[ -f "$LOCAL_PROPS" ]]; then
    if grep -q "ffmpegPrebuiltDir" "$LOCAL_PROPS"; then
        if [[ "$(uname)" == "Darwin" ]]; then
            sed -i '' "s|^ffmpegPrebuiltDir=.*|${PROP_LINE}|" "$LOCAL_PROPS"
        else
            sed -i "s|^ffmpegPrebuiltDir=.*|${PROP_LINE}|" "$LOCAL_PROPS"
        fi
    else
        printf "\n%s\n" "$PROP_LINE" >> "$LOCAL_PROPS"
    fi
else
    echo "$PROP_LINE" > "$LOCAL_PROPS"
fi
echo "  local.properties updated: $PROP_LINE"
echo ""
echo "  Next: ./gradlew :android:assembleDebug"
