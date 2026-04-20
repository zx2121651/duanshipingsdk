# Phase-1 Minimum Verification Checklist

This checklist defines the **mandatory** minimum verification steps required for every contributor (Human or Agent) before submitting any code changes.

## 1. Core C++ Engine (Mandatory)
Required for any changes affecting `core/`, `tests/`, or build configurations.

- [ ] **Build Core (Headless)**: `cmake -B build -S . -DUSE_MOCK_GL=ON && cmake --build build`
- [ ] **Run Core Tests**: `cd build && ctest --output-on-failure` (Must pass all 9+ suites)

## 2. Android SDK (Mandatory)
Required for any changes affecting `android/`, JNI, or core logic.

- [ ] **Run Unit Tests**: `./gradlew :android:testDebugUnitTest`
- [ ] **Assemble SDK**: `./gradlew :android:assembleDebug` (Verifies library build)

## 3. iOS SDK (Mandatory)
Required for any changes affecting `ios/` or core logic.

- [ ] **Build Framework**:
  ```bash
  xcodebuild clean build \
    -project ios/VideoSDK.xcodeproj \
    -scheme "VideoSDK" \
    -destination "generic/platform=iOS Simulator" \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGNING_ALLOWED=NO
  ```

## 4. Documentation & Hygiene (Mandatory)
- [ ] **Update ARCHITECTURE.md**: If adding/changing major modules or flows.
- [ ] **Update MEMORY**: If the change introduces new critical context for future tasks.
- [ ] **Branch Naming**: Use descriptive branch names (e.g., `feature/xxx`, `fix/xxx`).

## 5. Environment-Dependent Verification (Recommended)
Perform these if your local environment supports it.

- [ ] **Android Integration**: `./gradlew :sample-android:assembleDebug` (Verifies Sample App)
- [ ] **Real GL Tests**: `./build/test_headless_ssim` (Requires GPU/Mesa/ANGLE)

---
*Note: This checklist is enforced by the project's Phase-1 quality standards. For environment setup instructions, refer to [BUILD.md](./BUILD.md).*
