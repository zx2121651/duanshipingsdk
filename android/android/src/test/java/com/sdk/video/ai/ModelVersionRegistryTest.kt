package com.sdk.video.ai

import android.content.Context
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

@RunWith(MockitoJUnitRunner::class)
class ModelVersionRegistryTest {

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

    // TC-R01: put + get round-trip
    @Test
    fun `put and get returns stored ModelInfo`() {
        val info = ModelVersionRegistry.ModelInfo(
            version   = "20240101",
            sha256    = "abc123",
            sizeBytes = 1024L,
            source    = "assets"
        )
        registry.put("face_landmark.tflite", info)

        val result = registry.get("face_landmark.tflite")
        assertNotNull(result)
        assertEquals("20240101", result!!.version)
        assertEquals("abc123",   result.sha256)
        assertEquals(1024L,      result.sizeBytes)
        assertEquals("assets",   result.source)
    }

    // TC-R02: isUpToDate returns true when version matches
    @Test
    fun `isUpToDate returns true for matching version`() {
        registry.put("model.tflite", ModelVersionRegistry.ModelInfo(version = "1.0.0"))
        assertTrue(registry.isUpToDate("model.tflite", "1.0.0"))
    }

    // TC-R03: isUpToDate returns false when version differs
    @Test
    fun `isUpToDate returns false for different version`() {
        registry.put("model.tflite", ModelVersionRegistry.ModelInfo(version = "1.0.0"))
        assertFalse(registry.isUpToDate("model.tflite", "2.0.0"))
    }

    // TC-R04: isUpToDate returns false for unknown model
    @Test
    fun `isUpToDate returns false for unknown model`() {
        assertFalse(registry.isUpToDate("nonexistent.tflite", "1.0.0"))
    }

    // TC-R05: persistence — manifest survives registry re-creation
    @Test
    fun `manifest persists across registry instances`() {
        registry.put("seg.tflite", ModelVersionRegistry.ModelInfo(
            version = "20240601", sha256 = "deadbeef", source = "ota"
        ))

        // Re-create registry from same filesDir
        val registry2 = ModelVersionRegistry(context)
        val loaded = registry2.get("seg.tflite")
        assertNotNull("Model should survive reload", loaded)
        assertEquals("20240601",  loaded!!.version)
        assertEquals("deadbeef",  loaded.sha256)
        assertEquals("ota",       loaded.source)
    }

    // TC-R06: remove clears entry from cache and disk
    @Test
    fun `remove deletes entry from registry`() {
        registry.put("hair.tflite", ModelVersionRegistry.ModelInfo(version = "1.0"))
        assertNotNull(registry.get("hair.tflite"))

        registry.remove("hair.tflite")
        assertNull(registry.get("hair.tflite"))

        // Also check persistence
        val registry2 = ModelVersionRegistry(context)
        assertNull("Entry should be absent after reload", registry2.get("hair.tflite"))
    }

    // TC-R07: verifyChecksum returns true when sha256 is empty (skip check)
    @Test
    fun `verifyChecksum returns true when sha256 is empty`() {
        val tmpFile = tmpFolder.newFile("model.tflite").apply { writeText("data") }
        registry.put("model.tflite", ModelVersionRegistry.ModelInfo(version = "1.0", sha256 = ""))
        assertTrue("Empty sha256 should skip verification", registry.verifyChecksum(tmpFile, "model.tflite"))
    }

    // TC-R08: computeSha256 returns consistent hex digest
    @Test
    fun `computeSha256 returns deterministic result`() {
        val content = "hello world"
        val file = tmpFolder.newFile("test.bin").apply { writeText(content) }
        val hash1 = registry.computeSha256(file)
        val hash2 = registry.computeSha256(file)
        assertEquals("SHA-256 must be deterministic", hash1, hash2)
        assertEquals("Expected length 64 hex chars", 64, hash1.length)
        // Known SHA-256 of "hello world\n" (or "hello world" depending on writeText)
        assertFalse("Hash must be non-empty", hash1.isEmpty())
    }

    // TC-R09: computeSha256 returns empty string for missing file
    @Test
    fun `computeSha256 returns empty string for nonexistent file`() {
        val missing = File(tmpFolder.root, "ghost.tflite")
        assertEquals("", registry.computeSha256(missing))
    }

    // TC-R10: all() returns snapshot of all entries
    @Test
    fun `all() returns all registered entries`() {
        registry.put("a.tflite", ModelVersionRegistry.ModelInfo(version = "1"))
        registry.put("b.tflite", ModelVersionRegistry.ModelInfo(version = "2"))
        registry.put("c.tflite", ModelVersionRegistry.ModelInfo(version = "3"))

        val all = registry.all()
        assertEquals(3, all.size)
        assertTrue(all.containsKey("a.tflite"))
        assertTrue(all.containsKey("b.tflite"))
        assertTrue(all.containsKey("c.tflite"))
    }
}
