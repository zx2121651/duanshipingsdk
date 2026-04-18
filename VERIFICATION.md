# Phase-1 Minimum Local Verification Guide

This guide defines the mandatory minimum verification steps required before submitting any code changes.

## 1. Core C++ Engine (MVP)
**Mandatory** for any changes affecting `core/` or `tests/`.

- [ ] **Build & Run Core Tests (Headless)**
  ```bash
  cmake -B build -S . -DUSE_MOCK_GL=ON
  cmake --build build
  cd build && ctest --output-on-failure
  ```

## 2. Android (MVP)
**Mandatory** for any changes affecting `android/`, JNI, or core logic.

- [ ] **Run SDK Unit Tests**
  ```bash
  ./gradlew :android:testDebugUnitTest
  ```
- [ ] **Assemble SDK Library**
  ```bash
  ./gradlew :android:assembleDebug
  ```

## 3. iOS (MVP)
**Mandatory** for any changes affecting `ios/` or core logic.

- [ ] **Build Framework**
  ```bash
  xcodebuild clean build \
    -project ios/VideoSDK.xcodeproj \
    -scheme "VideoSDK" \
    -destination "generic/platform=iOS Simulator" \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGNING_ALLOWED=NO
  ```

## 4. Environment-Dependent Verification
Recommended if the specific environment is available.

- **Rendering Tests (Linux/Windows with GPU/ANGLE):**
  ```bash
  # Ensure USE_MOCK_GL=OFF
  ./build/test_headless_ssim
  ```
- **Android Integration Check:**
  ```bash
  ./gradlew :sample-android:assembleDebug
  ```
