# Local Build Prerequisites & Setup Guide

This document outlines the environment requirements and build instructions for the ShortVideo SDK.

---

## 1. Core C++ Engine (video_sdk_core)

The core engine is cross-platform and uses CMake as the build system.

### Prerequisites (Mandatory)
- **CMake**: Version 3.10.2 or higher.
- **C++ Compiler**: Support for C++17 (e.g., GCC 7+, Clang 5+, or MSVC 2017+).

### Optional Dependencies
- **OpenGL ES 2.0/3.0**: Required for real rendering tests.
    - **Linux**: `libgles2-mesa-dev`, `libegl1-mesa-dev`.
    - **Windows**: [ANGLE](https://github.com/google/angle) (can be installed via `vcpkg`).
- **vcpkg**: Recommended for managing dependencies on Windows.

### Build Commands
```bash
cmake -B build -S .
cmake --build build --config Release
cd build && ctest
```

---

## 2. Android SDK & Sample

### Prerequisites (Mandatory)
- **JDK 17**: Ensure `JAVA_HOME` is set.
- **Android SDK**: `compileSdk 34`, `minSdk 24`.
- **Android NDK**: Version 25.1.8937393 (suggested) or matching the CMake configuration.
- **CMake**: Version 3.22.1 (installed via Android SDK Manager).

### Build Commands
```bash
# Build SDK Library
./gradlew :android:assembleDebug

# Build Sample App
./gradlew :sample-android:assembleDebug

# Run Unit Tests
./gradlew :android:testDebugUnitTest
```

---

## 3. iOS SDK

### Prerequisites (Mandatory)
- **macOS**: Required for iOS development.
- **Xcode**: Latest stable version recommended.
- **Command Line Tools**: `xcodebuild` should be available.

### Build Commands
```bash
xcodebuild clean build \
  -project ios/VideoSDK.xcodeproj \
  -scheme "VideoSDK" \
  -destination "generic/platform=iOS Simulator" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO
```

---

## 4. Quick Selection Guide

| Task | Minimum Requirements | Recommended Path |
| :--- | :--- | :--- |
| **Logic Verification** | CMake + C++ Compiler | Build `video_sdk_core` with `USE_MOCK_GL=ON` |
| **Android Dev** | JDK 17 + Android SDK | Use Android Studio or `./gradlew` |
| **iOS Dev** | macOS + Xcode | Use `ios/VideoSDK.xcodeproj` |
| **Rendering Tests** | OpenGL ES + EGL | Run `test_headless_ssim` |

---

## 5. Troubleshooting
- **Windows Build**: If CMake fails to find OpenGL, run `setup_windows_env.ps1` to setup vcpkg and ANGLE.
- **Gradle Errors**: Ensure `./gradlew` has execution permissions (`chmod +x gradlew`).
