# Consumer ProGuard rules for applications integrating ShortVideoSDK.
# Keep JNI-facing classes and native method signatures reachable after shrinking.
-keepclasseswithmembernames class * {
    native <methods>;
}

-keep class com.sdk.video.** { *; }
-dontwarn com.sdk.video.**
