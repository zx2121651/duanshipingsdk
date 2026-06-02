# Android Integration

## Artifact

The Android SDK module is `:android`. It produces:

```text
android/build/outputs/aar/android-release.aar
```

The release package renames this artifact to:

```text
android/ShortVideoSDK.aar
```

## Gradle Setup

Place the AAR under your app module, for example:

```text
app/libs/ShortVideoSDK.aar
```

Then add:

```groovy
dependencies {
    implementation files('libs/ShortVideoSDK.aar')

    implementation 'androidx.core:core-ktx:1.12.0'
    implementation 'androidx.appcompat:appcompat:1.6.1'
    implementation 'com.google.android.material:material:1.11.0'

    def camerax_version = "1.3.1"
    implementation "androidx.camera:camera-core:${camerax_version}"
    implementation "androidx.camera:camera-camera2:${camerax_version}"
    implementation "androidx.camera:camera-lifecycle:${camerax_version}"
    implementation "androidx.camera:camera-view:${camerax_version}"

    implementation 'androidx.work:work-runtime-ktx:2.9.0'
}
```

## Requirements

- `minSdk`: 24
- `targetSdk`: 34
- Java 17
- Android Gradle Plugin 8.2.1
- Android NDK 25.1.8937393 when rebuilding native code
- CMake 3.22.1 when rebuilding native code

## Sample

The Android sample lives at:

```text
samples/android
```

Run it from the source workspace with:

```powershell
.\gradlew.bat :sample-android:assembleDebug
```
