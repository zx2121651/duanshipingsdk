package com.sdk.video

/**
 * Unified Error Model for the SDK.
 * Maps C++ Native ErrorCode (from core/include/GLTypes.h) to Kotlin Exceptions.
 */
sealed class VideoSdkError(message: String, val code: Int) : Exception("[$code] $message") {
    // 1000 - 1999: Initialization errors
    class InitContextFailed : VideoSdkError("Failed to initialize GL Context", -1001)
    class InitShaderFailed : VideoSdkError("Failed to initialize Shader", -1002)
    class InitOboeFailed : VideoSdkError("Failed to initialize Oboe Audio", -1003)

    // 2000 - 2999: Rendering errors
    class RenderFboAllocFailed : VideoSdkError("Failed to allocate FBO", -2001)
    class RenderInvalidState : VideoSdkError("Render Engine is in an invalid state", -2002)
    class RenderComputeNotSupported : VideoSdkError("Compute Shaders are not supported on this device", -2003)

    // 3000 - 3999: Timeline errors
    class TimelineNull : VideoSdkError("Timeline is null", -3001)
    class TimelineTrackNotFound : VideoSdkError("Timeline Track not found", -3002)
    class TimelineClipNotFound : VideoSdkError("Timeline Clip not found", -3003)

    // General or Unknown errors
    class UnknownNativeError(code: Int) : VideoSdkError("Unknown native error", code)
    class InvalidOperation(message: String) : VideoSdkError(message, -1)
    class NativeError(code: Int, val nativeMessage: String) : VideoSdkError(nativeMessage, code)

    companion object {
        fun fromNativeCode(code: Int): VideoSdkError {
            return when (code) {
                -1001 -> InitContextFailed()
                -1002 -> InitShaderFailed()
                -1003 -> InitOboeFailed()
                -2001 -> RenderFboAllocFailed()
                -2002 -> RenderInvalidState()
                -2003 -> RenderComputeNotSupported()
                -3001 -> TimelineNull()
                -3002 -> TimelineTrackNotFound()
                -3003 -> TimelineClipNotFound()
                else -> UnknownNativeError(code)
            }
        }

        fun <T> toResult(code: Int, successValue: T): Result<T> {
            return if (code == 0) {
                Result.success(successValue)
            } else {
                Result.failure(fromNativeCode(code))
            }
        }

        fun toResult(code: Int): Result<Unit> {
            return toResult(code, Unit)
        }
    }
}