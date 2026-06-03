package com.sdk.video

import android.os.Build
import java.util.Locale

/**
 * Device-specific preview workarounds.
 *
 * Most preview orientation/crop fixes should be deterministic and shared by all
 * devices. This table is only for vendor or driver behavior that cannot be
 * detected from CameraX/SurfaceTexture metadata alone.
 */
@InternalApi
object DeviceQuirks {
    data class PreviewQuirks(
        val manufacturer: String,
        val model: String,
        val sdkInt: Int,
        val glVendor: String?,
        val glRenderer: String?,
        val glVersion: String?,
        val applySurfaceTextureMatrixOnDisplay: Boolean,
        val reason: String
    ) {
        fun describe(): String {
            return "manufacturer=$manufacturer, model=$model, sdk=$sdkInt, " +
                "glVendor=${glVendor ?: "unknown"}, glRenderer=${glRenderer ?: "unknown"}, " +
                "applyDisplayMatrix=$applySurfaceTextureMatrixOnDisplay, reason=$reason"
        }
    }

    fun detect(
        glVendor: String? = null,
        glRenderer: String? = null,
        glVersion: String? = null
    ): PreviewQuirks {
        val manufacturer = Build.MANUFACTURER.orEmpty()
        val model = Build.MODEL.orEmpty()
        val deviceKey = "${manufacturer.lowercase(Locale.US)} ${model.lowercase(Locale.US)}"
        val rendererKey = "${glVendor.orEmpty()} ${glRenderer.orEmpty()} ${glVersion.orEmpty()}"
            .lowercase(Locale.US)

        // Native OES2RGB already consumes SurfaceTexture.getTransformMatrix().
        // Applying that matrix again in the display pass causes double rotation
        // and double crop. Keep this false unless a verified device requires the
        // display pass to sample from an uncorrected camera texture.
        val needsDisplayMatrix = knownDisplayMatrixDevices.any { pattern ->
            deviceKey.contains(pattern) || rendererKey.contains(pattern)
        }

        return PreviewQuirks(
            manufacturer = manufacturer,
            model = model,
            sdkInt = Build.VERSION.SDK_INT,
            glVendor = glVendor,
            glRenderer = glRenderer,
            glVersion = glVersion,
            applySurfaceTextureMatrixOnDisplay = needsDisplayMatrix,
            reason = if (needsDisplayMatrix) {
                "matched known display-matrix quirk"
            } else {
                "native OES pass owns SurfaceTexture matrix"
            }
        )
    }

    // Keep this list intentionally empty until we verify a real device. The
    // hook exists so future model-specific fixes can be added without changing
    // the rendering pipeline again.
    private val knownDisplayMatrixDevices = emptySet<String>()
}
