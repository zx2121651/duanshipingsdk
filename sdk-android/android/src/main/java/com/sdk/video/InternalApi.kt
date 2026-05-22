
package com.sdk.video

@RequiresOptIn(
    level = RequiresOptIn.Level.ERROR,
    message = "This API is internal to the SDK and should not be called directly."
)
@Retention(AnnotationRetention.BINARY)
@Target(AnnotationTarget.CLASS, AnnotationTarget.FUNCTION, AnnotationTarget.PROPERTY)
annotation class InternalApi
