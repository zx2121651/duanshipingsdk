package com.sdk.video.ai

import android.content.Context
import android.content.res.AssetManager
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.Mockito.*
import org.mockito.junit.MockitoJUnitRunner
import java.io.ByteArrayInputStream
import java.io.File

@RunWith(MockitoJUnitRunner::class)
class ModelAssetManagerTest {

    @get:Rule
    val tmpFolder = TemporaryFolder()

    @Mock
    private lateinit var context: Context

    @Mock
    private lateinit var assetManager: AssetManager

    private lateinit var filesDir: File

    @Before
    fun setUp() {
        filesDir = tmpFolder.newFolder("filesDir")
        `when`(context.filesDir).thenReturn(filesDir)
        `when`(context.assets).thenReturn(assetManager)
    }

    @Test
    fun `extractModel returns Stub for stub placeholder content`() = runTest {
        val stubContent = "STUB_TFLITE_MODEL_v1\nModel: test\n"
        `when`(assetManager.open("models/face_landmark_stub.tflite"))
            .thenReturn(ByteArrayInputStream(stubContent.toByteArray()))

        val mgr = ModelAssetManager(context)
        val result = mgr.extractModel("models/face_landmark_stub.tflite")

        assertTrue("Expected Stub result", result is ModelAssetManager.ModelLoadResult.Stub)
    }

    @Test
    fun `extractModel returns Ready for non-stub binary content`() = runTest {
        // Simulate a real (non-stub) binary tflite - first line does not start with STUB_
        val binaryContent = byteArrayOf(0x18, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4C, 0x33) // TFL3 magic
        `when`(assetManager.open("models/real_model.tflite"))
            .thenReturn(ByteArrayInputStream(binaryContent))

        val mgr = ModelAssetManager(context)
        val result = mgr.extractModel("models/real_model.tflite")

        assertTrue("Expected Ready result", result is ModelAssetManager.ModelLoadResult.Ready)
        val ready = result as ModelAssetManager.ModelLoadResult.Ready
        assertTrue("Path should exist", File(ready.path).exists())
    }

    @Test
    fun `extractModel returns Error when asset not found`() = runTest {
        `when`(assetManager.open("models/nonexistent.tflite"))
            .thenThrow(java.io.IOException("File not found"))

        val mgr = ModelAssetManager(context)
        val result = mgr.extractModel("models/nonexistent.tflite")

        assertTrue("Expected Error result", result is ModelAssetManager.ModelLoadResult.Error)
    }

    @Test
    fun `extractModel caches file and skips re-extraction on second call`() = runTest {
        val stubContent = "STUB_TFLITE_MODEL_v1\nModel: test\n"
        `when`(assetManager.open("models/face_landmark_stub.tflite"))
            .thenReturn(ByteArrayInputStream(stubContent.toByteArray()))

        val mgr = ModelAssetManager(context)
        mgr.extractModel("models/face_landmark_stub.tflite") // first call extracts
        mgr.extractModel("models/face_landmark_stub.tflite") // second call should use cache

        // assetManager.open() should only have been called once
        verify(assetManager, times(1)).open("models/face_landmark_stub.tflite")
    }

    @Test
    fun `clearCache deletes all extracted files`() = runTest {
        val content = "STUB_TFLITE_MODEL_v1\n"
        `when`(assetManager.open("models/face_landmark_stub.tflite"))
            .thenReturn(ByteArrayInputStream(content.toByteArray()))

        val mgr = ModelAssetManager(context)
        mgr.extractModel("models/face_landmark_stub.tflite")

        val modelsDir = File(filesDir, "models")
        assertTrue("models dir should exist after extraction", modelsDir.exists())
        assertTrue("Should have files", modelsDir.listFiles()?.isNotEmpty() == true)

        mgr.clearCache()
        assertTrue("models dir empty after clearCache",
            modelsDir.listFiles()?.isEmpty() != false)
    }
}
