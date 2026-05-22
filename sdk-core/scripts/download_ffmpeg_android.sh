#!/usr/bin/env bash
# download_ffmpeg_android.sh — thin wrapper; delegates to build_ffmpeg_android.sh
#
# Builds FFmpeg 6.0 from source using the Android NDK (found automatically).
#
# Usage:
#   ./scripts/download_ffmpeg_android.sh [<out-dir>]
#
# Example:
#   ./scripts/download_ffmpeg_android.sh
#   ./scripts/download_ffmpeg_android.sh /opt/libs/ffmpeg

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Pass optional out-dir override via env
if [[ -n "${1:-}" ]]; then
    export FFMPEG_OUT_DIR="$1"
fi

exec bash "$SCRIPT_DIR/build_ffmpeg_android.sh"
