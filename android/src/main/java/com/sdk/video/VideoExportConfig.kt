package com.sdk.video

/**
 * Configuration options for Video Export/Recording.
 */
data class VideoExportConfig(
    val width: Int = 1080,
    val height: Int = 1920,
    val fps: Int = 30,
    val videoBitrate: Int = 10_000_000, // 10 Mbps
    val audioBitrate: Int = 128_000,    // 128 Kbps
    val audioSampleRate: Int = 44100,
    val iFrameInterval: Int = 1,        // GOP interval in seconds (e.g. 1 sec = 30 frames)
    val outputPath: String
)
