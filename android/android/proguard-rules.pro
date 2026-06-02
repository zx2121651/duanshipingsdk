# Internal shrinker rules for the Android SDK module.
# Public consumer rules live in consumer-rules.pro and are packaged into the AAR.

# ---- Compose Runtime — SnapshotStateList lock verification fix ----
# Without these keeps, R8/D8 may optimize Compose runtime internals in ways
# that break ART's lock verification pass, causing "failed lock verification
# and will run slower" warnings for SnapshotStateList methods.
-keep class androidx.compose.runtime.snapshots.** { *; }
-dontwarn androidx.compose.runtime.snapshots.**

# ---- General Compose runtime stability ----
# Compose uses reflection-like patterns in snapshot state tracking.
# Aggressive R8 shrinking can cause runtime warnings and performance
# regressions in Debug builds.
-keepattributes LineNumberTable,SourceFile
-keep class kotlinx.coroutines.** { *; }
-dontwarn kotlinx.coroutines.**
