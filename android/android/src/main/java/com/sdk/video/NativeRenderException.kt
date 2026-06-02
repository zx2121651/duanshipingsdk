package com.sdk.video

import androidx.annotation.Keep

@Keep
class NativeRenderException(val errorCode: Int, message: String) : RuntimeException(message)
