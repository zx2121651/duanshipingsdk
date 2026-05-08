package com.sdk.video.sample

import android.app.Application
import android.util.Log
import com.sdk.video.CrashReporter

class SampleApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        CrashReporter.init(applicationContext, anrThresholdMs = 5_000L)
        CrashReporter.addSink { report ->
            Log.w("SampleCrashReporter", "${report.type}: ${report.summary}")
        }
    }
}
