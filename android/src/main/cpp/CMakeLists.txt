cmake_minimum_required(VERSION 3.10.2)
project("video-sdk")

# 引入 FetchContent 用于下载 Oboe
include(FetchContent)
FetchContent_Declare(
        oboe
        GIT_REPOSITORY https://github.com/google/oboe.git
        GIT_TAG 1.8.0
)
FetchContent_MakeAvailable(oboe)

# 定义核心源码文件
set(CORE_SRC
        ../../../../core/src/Filter.cpp
        ../../../../core/src/FilterEngine.cpp
        ../../../../core/src/Filters.cpp
        ../../../../core/src/FrameBuffer.cpp
        ../../../../core/src/FrameBufferPool.cpp
        ../../../../core/src/GLContextManager.cpp
        ../../../../core/src/GLStateManager.cpp
        ../../../../core/src/ShaderManager.cpp
        ../../../../core/src/ThreadCheck.cpp
        ../../../../core/src/pipeline/PipelineGraph.cpp
        AndroidAssetProvider.cpp
        ../../../../core/src/timeline/Clip.cpp
        ../../../../core/src/timeline/Track.cpp
        ../../../../core/src/timeline/Timeline.cpp
        ../../../../core/src/timeline/Compositor.cpp
        ../../../../core/src/timeline/DecoderPool.cpp
        ../../../../core/src/timeline/AudioMixer.cpp
        ../../../../core/src/timeline/AudioEffects.cpp
        ../../../../core/src/timeline/AudioDecoderAndroid.cpp
        ../../../../core/src/timeline/VideoDecoderAndroid.cpp
        ../../../../core/src/timeline/TimelineExporterAndroid.cpp
        ../../../../core/src/timeline/FFmpegVideoDecoder.cpp
        ../../../../core/src/ai/TfliteInferenceEngine.cpp
        ../../../../core/src/ai/SegmentationFilter.cpp
        ../../../../core/src/ai/BeautyFilter.cpp
        ../../../../core/src/ai/FaceLandmarkDetector.cpp
        ../../../../core/src/ai/FaceReshapeFilter.cpp
        ../../../../core/src/ai/MakeupFilter.cpp
        ../../../../core/src/ai/HairSegmentationFilter.cpp
        ../../../../core/src/EffectPlugin.cpp
        ../../../../core/src/rhi/GLTexture.cpp
        ../../../../core/src/rhi/GLRenderDevice.cpp
        ../../../../core/src/rhi/GLBuffer.cpp
        ../../../../core/src/rhi/GLVertexArray.cpp
        ../../../../core/src/rhi/GLShaderProgram.cpp
        ../../../../core/src/rhi/RenderDeviceFactory.cpp
)

add_library(video-sdk SHARED
    NativeBridge.cpp
    OboeAudioEngine.cpp
    TimelineBridge.cpp
    TimelineExporterBridge.cpp
    TimelinePreviewBridge.cpp
    ${CORE_SRC}
)

# 包含路径
target_include_directories(video-sdk PRIVATE
    ../../../../core/include
    ${oboe_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# Vulkan RHI 后端（默认开启，要求 NDK >= 25）
# 如需禁用：在 gradle 或 CMake 调用层传入 -DWITH_VULKAN=OFF
# ---------------------------------------------------------------------------
option(WITH_VULKAN "Enable Vulkan RHI backend" ON)
if(WITH_VULKAN)
    target_compile_definitions(video-sdk PRIVATE HAS_VULKAN)
    target_sources(video-sdk PRIVATE
        ../../../../core/src/rhi/vk/VulkanContext.cpp
        ../../../../core/src/rhi/vk/VulkanMemoryAllocator.cpp
        ../../../../core/src/rhi/vk/VulkanBuffer.cpp
        ../../../../core/src/rhi/vk/VulkanTexture.cpp
        ../../../../core/src/rhi/vk/VulkanShaderProgram.cpp
        ../../../../core/src/rhi/vk/VulkanCommandBuffer.cpp
        ../../../../core/src/rhi/vk/VulkanRenderDevice.cpp
    )
    message(STATUS "Android: Vulkan RHI backend ENABLED")
endif()

# ---------------------------------------------------------------------------
# FFmpeg 软解集成（可选）
#
# 使用方式：在 build.gradle 的 externalNativeBuild arguments 中传入：
#   -DFFMPEG_PREBUILT_DIR=${projectDir}/src/main/jniLibs/ffmpeg
#
# 预编译库目录结构（per-ABI）：
#   jniLibs/ffmpeg/
#     arm64-v8a/
#       libavcodec.so  libavformat.so  libavutil.so  libswscale.so
#     armeabi-v7a/
#       ...
#
# 如果目录不存在，则跳过 FFmpeg 集成，SoftwareVideoDecoder 保持 stub 状态。
# ---------------------------------------------------------------------------
if(DEFINED FFMPEG_PREBUILT_DIR
   AND EXISTS "${FFMPEG_PREBUILT_DIR}/include/libavcodec/avcodec.h"
   AND EXISTS "${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/libavcodec.so"
   AND EXISTS "${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/libavformat.so"
   AND EXISTS "${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/libavutil.so"
   AND EXISTS "${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/libswscale.so")
    message(STATUS "[video-sdk] FFmpeg prebuilt found at ${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI} — enabling HAS_FFMPEG_DECODER")

    target_compile_definitions(video-sdk PRIVATE HAS_FFMPEG_DECODER)
    target_include_directories(video-sdk PRIVATE ${FFMPEG_PREBUILT_DIR}/include)

    add_library(avcodec  SHARED IMPORTED)
    add_library(avformat SHARED IMPORTED)
    add_library(avutil   SHARED IMPORTED)
    add_library(swscale  SHARED IMPORTED)

    set_target_properties(avcodec  PROPERTIES IMPORTED_LOCATION "${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/libavcodec.so")
    set_target_properties(avformat PROPERTIES IMPORTED_LOCATION "${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/libavformat.so")
    set_target_properties(avutil   PROPERTIES IMPORTED_LOCATION "${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/libavutil.so")
    set_target_properties(swscale  PROPERTIES IMPORTED_LOCATION "${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/libswscale.so")

    target_link_libraries(video-sdk avcodec avformat avutil swscale)
else()
    message(STATUS "[video-sdk] FFmpeg prebuilt NOT found — SoftwareVideoDecoder remains stub")
    message(STATUS "           To enable: -DFFMPEG_PREBUILT_DIR=<path> in externalNativeBuild arguments")
endif()

# ---------------------------------------------------------------------------
# TFLite AI 推理（可选）
#
# 使用方式：在 build.gradle externalNativeBuild arguments 中传入：
#   -DTFLITE_PREBUILT_DIR=${projectDir}/src/main/jniLibs/tflite
#
# 预编译库目录结构：
#   jniLibs/tflite/
#     include/                        ← TFLite 头文件
#     arm64-v8a/libtensorflowlite.so
#     arm64-v8a/libtensorflowlite_gpu_delegate.so   (可选 GPU 加速)
# ---------------------------------------------------------------------------
if(DEFINED TFLITE_PREBUILT_DIR AND EXISTS "${TFLITE_PREBUILT_DIR}/${ANDROID_ABI}/libtensorflowlite.so")
    message(STATUS "[video-sdk] TFLite prebuilt found — enabling HAS_TFLITE")
    target_compile_definitions(video-sdk PRIVATE HAS_TFLITE)
    target_include_directories(video-sdk PRIVATE ${TFLITE_PREBUILT_DIR}/include)

    add_library(tensorflowlite SHARED IMPORTED)
    set_target_properties(tensorflowlite PROPERTIES
        IMPORTED_LOCATION "${TFLITE_PREBUILT_DIR}/${ANDROID_ABI}/libtensorflowlite.so")
    target_link_libraries(video-sdk tensorflowlite)

    # GPU Delegate（可选）
    if(EXISTS "${TFLITE_PREBUILT_DIR}/${ANDROID_ABI}/libtensorflowlite_gpu_delegate.so")
        add_library(tensorflowlite_gpu_delegate SHARED IMPORTED)
        set_target_properties(tensorflowlite_gpu_delegate PROPERTIES
            IMPORTED_LOCATION "${TFLITE_PREBUILT_DIR}/${ANDROID_ABI}/libtensorflowlite_gpu_delegate.so")
        target_link_libraries(video-sdk tensorflowlite_gpu_delegate)
        target_compile_definitions(video-sdk PRIVATE HAS_TFLITE_GPU_DELEGATE)
    endif()
else()
    message(STATUS "[video-sdk] TFLite prebuilt NOT found — AI inference uses stub")
    message(STATUS "           To enable: -DTFLITE_PREBUILT_DIR=<path> in externalNativeBuild arguments")
endif()

# 基础依赖库
target_link_libraries(video-sdk
        android
        log
        EGL
        GLESv3
        oboe
        OpenSLES
        mediandk
)

# Vulkan 运行时链接（WITH_VULKAN=ON 时）
if(WITH_VULKAN)
    target_link_libraries(video-sdk vulkan)
endif()
