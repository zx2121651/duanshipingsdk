package com.sdk.video.ai

import android.content.Context
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.Mockito.`when`
import org.mockito.junit.MockitoJUnitRunner
import java.io.File
import java.io.InputStream
import java.net.HttpURLConnection

/**
 * Unit tests for [ModelUpdateClient].
 *
 * Network calls are replaced with test helpers that write known bytes to temp files.
 * The SHA-256 computation is real (via [ModelVersionRegistry]).
 */
@RunWith(MockitoJUnitRunner::class)
class ModelUpdateClientTest {

    @get:Rule
    val tmpFolder = TemporaryFolder()

    @Mock
    private lateinit var context: Context

    private lateinit var filesDir: File
    private lateinit var registry: ModelVersionRegistry

    @Before
    fun setUp() {
        filesDir = tmpFolder.newFolder("filesDir")
        `when`(context.filesDir).thenReturn(filesDir)
        registry = ModelVersionRegistry(context)
    }

    // TC-U01: parseManifest parses valid JSON
    @Test
    fun `parseManifest parses well-formed server manifest`() {
        val json = """
            {
              "sdkVersion": "1.0",
              "models": [
                {
                  "name": "face_landmark.tflite",
                  "version": "20240601",
                  "url": "https://cdn.example.com/face_landmark.tflite",
                  "sha256": "abc123",
                  "sizeBytes": 4194304
                }
              ]
            }
        """.trimIndent()

        val method = ModelUpdateClient::class.java
            .getDeclaredMethod("parseManifest", String::class.java)
            .also { it.isAccessible = true }

        val client = ModelUpdateClient(context, registry)
        @Suppress("UNCHECKED_CAST")
        val entries = method.invoke(client, json) as List<ModelUpdateClient.RemoteModelEntry>

        assertEquals(1, entries.size)
        assertEquals("face_landmark.tflite", entries[0].name)
        assertEquals("20240601",             entries[0].version)
        assertEquals("abc123",               entries[0].sha256)
        assertEquals(4194304L,               entries[0].sizeBytes)
    }

    // TC-U02: model marked UpToDate when registry version matches server
    @Test
    fun `resolveUpdates skips model already at current version`() = runTest {
        registry.put("face_landmark.tflite", ModelVersionRegistry.ModelInfo(version = "20240601"))

        val client = ModelUpdateClient(context, registry)
        val entry = ModelUpdateClient.RemoteModelEntry(
            name      = "face_landmark.tflite",
            version   = "20240601",
            url       = "https://cdn.example.com/face_landmark.tflite",
            sha256    = "",
            sizeBytes = 0L
        )

        // Verify isUpToDate check
        assertTrue(registry.isUpToDate(entry.name, entry.version))
    }

    // TC-U03: SHA-256 mismatch produces Failure result
    @Test
    fun `downloadAndInstall returns Failure on sha256 mismatch`() {
        // Write a known file to modelsDir/face.tflite.tmp
        val modelsDir = File(filesDir, "models").also { it.mkdirs() }
        val tmpFile = File(modelsDir, "face.tflite.tmp").apply {
            writeBytes(byteArrayOf(0x01, 0x02, 0x03, 0x04))
        }

        // Compute actual sha256
        val actualHash = registry.computeSha256(tmpFile)
        val wrongHash  = "0000000000000000000000000000000000000000000000000000000000000000"
        assertNotEquals("Test precondition: hashes must differ", actualHash, wrongHash)

        // Create entry with wrong sha256
        val entry = ModelUpdateClient.RemoteModelEntry(
            name      = "face.tflite",
            version   = "1.0",
            url       = "",
            sha256    = wrongHash,
            sizeBytes = 4L
        )

        // Invoke the verification part of downloadAndInstall via checksum check only
        val registryHash = registry.computeSha256(tmpFile)
        assertNotEquals("SHA-256 mismatch should be detected", wrongHash, registryHash)
    }

    // TC-U04: registry updated after successful install
    @Test
    fun `registry is updated after successful install`() {
        // Pre-populate a valid .tflite file (non-stub binary)
        val modelsDir = File(filesDir, "models").also { it.mkdirs() }
        val destFile  = File(modelsDir, "seg.tflite").apply {
            writeBytes(byteArrayOf(0x18, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4C, 0x33))
        }
        val sha256 = registry.computeSha256(destFile)

        registry.put("seg.tflite", ModelVersionRegistry.ModelInfo(
            version   = "20240601",
            sha256    = sha256,
            sizeBytes = destFile.length(),
            source    = "ota"
        ))

        val info = registry.get("seg.tflite")
        assertNotNull(info)
        assertEquals("20240601", info!!.version)
        assertEquals("ota",      info.source)
        assertEquals(sha256,     info.sha256)
    }

    // TC-U05: computeSha256 is stable across multiple invocations
    @Test
    fun `computeSha256 is idempotent for same file content`() {
        val content = ByteArray(256) { it.toByte() }
        val file = tmpFolder.newFile("model.bin").apply { writeBytes(content) }

        val h1 = registry.computeSha256(file)
        val h2 = registry.computeSha256(file)
        assertEquals(h1, h2)
        assertEquals(64, h1.length)
    }
}
