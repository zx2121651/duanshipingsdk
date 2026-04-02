package com.sdk.video

@RequiresOptIn(
    message = "This API is internal to the video SDK and should not be used outside.",
    level = RequiresOptIn.Level.ERROR
)
@Retention(AnnotationRetention.BINARY)
@Target(AnnotationTarget.CLASS, AnnotationTarget.FUNCTION, AnnotationTarget.PROPERTY)
annotation class InternalApi
