package com.sdk.video

import android.media.MediaCodecList
import android.media.MediaFormat
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.*
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
@OptIn(InternalApi::class)
class VideoEncoderTest {

    @Test
    fun testOptimizeCodecProfileFailure() {
        // Mock VideoFilterManager and VideoExportConfig
        val filterManager = mock(VideoFilterManager::class.java)
        val config = VideoExportConfig(
            width = 1280,
            height = 720,
            videoBitrate = 5000000,
            fps = 30,
            outputPath = "/sdcard/test.mp4"
        )

        val videoEncoder = VideoEncoder(filterManager, config)
        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, 1280, 720)

        // Use Mockito to mock MediaCodecList constructor to throw an exception
        mockConstruction(MediaCodecList::class.java) { mock, context ->
            throw RuntimeException("Simulated MediaCodecList failure")
        }.use {
            // Access the private method optimizeCodecProfileAndBitrateMode via reflection
            val method = VideoEncoder::class.java.getDeclaredMethod(
                "optimizeCodecProfileAndBitrateMode",
                MediaFormat::class.java
            )
            method.isAccessible = true

            // This should not throw an exception as it is caught inside VideoEncoder
            method.invoke(videoEncoder, format)
        }

        // If we reached here without exception, the test passed
    }
}
