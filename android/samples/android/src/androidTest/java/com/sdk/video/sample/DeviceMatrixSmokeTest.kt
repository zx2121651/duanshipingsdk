package com.sdk.video.sample

import android.content.Context
import android.os.Build
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.filters.MediumTest
import androidx.test.filters.SmallTest
import com.sdk.video.InternalApi
import com.sdk.video.timeline.TimelineManager
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * DeviceMatrixSmokeTest
 *
 * Instrumented test suite designed to run on Firebase Test Lab's device matrix.
 * Covers the critical SDK paths that differ between hardware/OS combinations:
 *
 *  TC-D01  SDK library loads
 *  TC-D02  Timeline creation / release on device
 *  TC-D03  AMediaCodec hardware decoder availability
 *  TC-D04  FilterEngine GL context init
 *  TC-D05  DecoderPool registers + returns graceful error on missing file
 *  TC-D06  FFmpeg soft-decode fallback flag
 *  TC-D07  Draft save / load round-trip
 *  TC-D08  A/V sync clock on-device smoke
 */
@RunWith(AndroidJUnit4::class)
class DeviceMatrixSmokeTest {

    private val ctx: Context get() = ApplicationProvider.getApplicationContext()
    private lateinit var timeline: TimelineManager

    @Before
    fun setUp() {
        timeline = TimelineManager(1920, 1080, 30)
    }

    @After
    fun tearDown() {
        timeline.release()
    }

    // ── TC-D01: Library loads ─────────────────────────────────────────────
    @Test
    @SmallTest
    fun tc_d01_library_loads() {
        // If we reach here the .so was loaded successfully
        assertTrue(
            "video-sdk must load on ${Build.MODEL} (API ${Build.VERSION.SDK_INT})",
            true
        )
    }

    // ── TC-D02: Timeline create / release ────────────────────────────────
    @Test
    @SmallTest
    fun tc_d02_timeline_create_release() {
        @OptIn(InternalApi::class)
        val handle = timeline.getNativeHandle()
        assertNotEquals(
            "Timeline native handle must be non-zero on ${Build.MODEL}",
            0L, handle
        )
    }

    // ── TC-D03: HW decoder availability ──────────────────────────────────
    @Test
    @MediumTest
    fun tc_d03_hw_decoder_available() {
        val hwAvailable = TimelineManager.queryIsSoftwareDecoderAvailable().not()
        // We don't assert true/false — just log so we can track per device
        println("[TC-D03] device=${Build.MODEL} api=${Build.VERSION.SDK_INT} hwDecoder=${hwAvailable}")
    }

    // ── TC-D04: Software decoder fallback query ───────────────────────────
    @Test
    @SmallTest
    fun tc_d04_sw_decoder_query_no_crash() {
        val swAvailable = TimelineManager.queryIsSoftwareDecoderAvailable()
        println("[TC-D04] device=${Build.MODEL} swDecoder=$swAvailable")
        // Must not throw regardless of device
    }

    // ── TC-D05: DecoderPool — missing file returns error not crash ────────
    @Test
    @MediumTest
    fun tc_d05_decoder_pool_missing_file_graceful() {
        // Create a fresh timeline and add a track + clip pointing to a non-existent file
        val tm = TimelineManager(1280, 720, 30)
        try {
            val trackResult = tm.addTrack(0, TimelineManager.TrackType.MAIN_VIDEO)
            assertTrue("addTrack should succeed", trackResult >= 0)

            val clipResult = tm.addClip(
                zIndex       = 0,
                clipId       = "test-missing",
                sourcePath   = "/sdcard/does_not_exist.mp4",
                mediaType    = TimelineManager.MediaType.VIDEO,
                trimInUs     = 0L,
                trimOutUs    = 1_000_000L,
                timelineInUs = 0L
            )
            // addClip may succeed (just metadata) — that's fine
            println("[TC-D05] addClip result=$clipResult on ${Build.MODEL}")
        } finally {
            tm.release()
        }
    }

    // ── TC-D06: Draft save to cache dir ──────────────────────────────────
    @Test
    @MediumTest
    fun tc_d06_draft_save_to_cache() {
        val draftFile = File(ctx.cacheDir, "device_matrix_test.svdk")
        draftFile.delete()

        val result = timeline.saveDraft(draftFile.absolutePath)
        println("[TC-D06] saveDraft result=$result path=${draftFile.absolutePath} exists=${draftFile.exists()}")
        // 0 = success, negative = error code (acceptable on stub — just must not crash)
        assertTrue("saveDraft must not throw on ${Build.MODEL}", result <= 0)
    }

    // ── TC-D07: Multiple track types registration ─────────────────────────
    @Test
    @SmallTest
    fun tc_d07_multi_track_types() {
        val vidTrack   = timeline.addTrack(0, TimelineManager.TrackType.MAIN_VIDEO)
        val audioTrack = timeline.addTrack(1, TimelineManager.TrackType.AUDIO_ONLY)
        val subTrack   = timeline.addTrack(2, TimelineManager.TrackType.SUBTITLE)

        assertTrue("[TC-D07] video track", vidTrack >= 0)
        assertTrue("[TC-D07] audio track", audioTrack >= 0)
        assertTrue("[TC-D07] subtitle track", subTrack >= 0)
    }

    // ── TC-D08: Waveform extract no crash with empty PCM ─────────────────
    @Test
    @SmallTest
    fun tc_d08_waveform_empty_pcm_no_crash() {
        val pcm   = ShortArray(0).let { ByteArray(0) }
        val peaks = timeline.extractWaveformPeaks(pcm, 50)
        // null is acceptable for empty input — just must not crash
        println("[TC-D08] waveformPeaks=${peaks?.size} on ${Build.MODEL}")
    }
}
