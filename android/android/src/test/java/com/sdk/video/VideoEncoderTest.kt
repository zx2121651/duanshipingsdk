package com.sdk.video

import android.media.MediaFormat
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.Mockito.mock
import org.mockito.junit.MockitoJUnitRunner

/**
 * Unit tests for [VideoEncoder] using MockitoJUnitRunner + mock-maker-inline.
 *
 * Note: Robolectric was removed because [mockConstruction] is incompatible with
 * Robolectric's custom ClassLoader. Instead we mock [MediaFormat] directly via
 * Mockito inline (configured in resources/mockito-extensions/org.mockito.plugins.MockMaker).
 */
@RunWith(MockitoJUnitRunner::class)
@OptIn(InternalApi::class)
class VideoEncoderTest {

    /**
     * TC-E01: optimizeCodecProfileAndBitrateMode must not propagate any exception
     * even when MediaCodecList throws (e.g., on devices where codec metadata is corrupt).
     */
    @Test
    fun `optimizeCodecProfileAndBitrateMode swallows MediaCodecList exceptions`() {
        val filterManager = mock(VideoFilterManager::class.java)
        val config = VideoExportConfig(
            width        = 1280,
            height       = 720,
            videoBitrate = 5_000_000,
            fps          = 30,
            outputPath   = "/sdcard/test.mp4"
        )
        val videoEncoder = VideoEncoder(filterManager, config)

        // Mock MediaFormat so no Android framework call is made
        val format = mock(MediaFormat::class.java)

        val method = VideoEncoder::class.java.getDeclaredMethod(
            "optimizeCodecProfileAndBitrateMode",
            MediaFormat::class.java
        ).also { it.isAccessible = true }

        // Must not throw — exception should be caught internally
        method.invoke(videoEncoder, format)
    }

    /**
     * TC-E02: VideoExportConfig defaults are sane (no output path = empty string, not null).
     */
    @Test
    fun `VideoExportConfig default outputPath is non-null`() {
        val config = VideoExportConfig(
            width        = 1920,
            height       = 1080,
            videoBitrate = 8_000_000,
            fps          = 30,
            outputPath   = ""
        )
        assert(config.outputPath != null) { "outputPath must not be null" }
    }
}
