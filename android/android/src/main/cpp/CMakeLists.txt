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
        ../../../../../sdk-core/core/src/Filter.cpp
        ../../../../../sdk-core/core/src/FilterEngine.cpp
        ../../../../../sdk-core/core/src/Filters.cpp
        ../../../../../sdk-core/core/src/PropOverlayFilter.cpp
        ../../../../../sdk-core/core/src/FrameBuffer.cpp
        ../../../../../sdk-core/core/src/FrameBufferPool.cpp
        ../../../../../sdk-core/core/src/GLContextManager.cpp
        ../../../../../sdk-core/core/src/GLStateManager.cpp
        ../../../../../sdk-core/core/src/ShaderManager.cpp
        ../../../../../sdk-core/core/src/ThreadCheck.cpp
        ../../../../../sdk-core/core/src/pipeline/PipelineGraph.cpp
        AndroidAssetProvider.cpp
        ../../../../../sdk-core/core/src/timeline/Clip.cpp
        ../../../../../sdk-core/core/src/timeline/Track.cpp
        ../../../../../sdk-core/core/src/timeline/Timeline.cpp
        ../../../../../sdk-core/core/src/timeline/Compositor.cpp
        ../../../../../sdk-core/core/src/timeline/DecoderPool.cpp
        ../../../../../sdk-core/core/src/timeline/AudioMixer.cpp
        ../../../../../sdk-core/core/src/timeline/AudioEffects.cpp
        ../../../../../sdk-core/core/src/timeline/AudioDecoderAndroid.cpp
        ../../../../../sdk-core/core/src/timeline/VideoDecoderAndroid.cpp
        ../../../../../sdk-core/core/src/timeline/TimelineExporterAndroid.cpp
        ../../../../../sdk-core/core/src/timeline/FFmpegVideoDecoder.cpp
        ../../../../../sdk-core/core/src/ai/TfliteInferenceEngine.cpp
        ../../../../../sdk-core/core/src/ai/SegmentationFilter.cpp
        ../../../../../sdk-core/core/src/ai/BeautyFilter.cpp
        ../../../../../sdk-core/core/src/ai/InferenceEngine.cpp
        ../../../../../sdk-core/core/src/ai/FaceLandmarkDetector.cpp
        ../../../../../sdk-core/core/src/ai/FaceReshapeFilter.cpp
        ../../../../../sdk-core/core/src/ai/MakeupFilter.cpp
        ../../../../../sdk-core/core/src/ai/HairSegmentationFilter.cpp
        ../../../../../sdk-core/core/src/ai/BodyPoseDetector.cpp
        ../../../../../sdk-core/core/src/ai/GestureRecognizer.cpp
        ../../../../../sdk-core/core/src/ai/StickerRenderer.cpp
        ../../../../../sdk-core/core/src/ai/HandLandmarkDetector.cpp
        ../../../../../sdk-core/core/src/ai/AutoSubtitleEngine.cpp
        ../../../../../sdk-core/core/src/ai/ChromaKeyFilter.cpp
        ../../../../../sdk-core/core/src/ai/ExpressionDetector.cpp
        ../../../../../sdk-core/core/src/ai/VideoStabilizer.cpp
        ../../../../../sdk-core/core/src/ai/ArFaceAnchor.cpp
        ../../../../../sdk-core/core/src/ai/ParticleSystem.cpp
        ../../../../../sdk-core/core/src/ai/ObjectTracker.cpp
        ../../../../../sdk-core/core/src/ai/FaceMorphFilter.cpp
        ../../../../../sdk-core/core/src/ai/BodyEffectFilter.cpp
        ../../../../../sdk-core/core/src/ai/DepthEstimator.cpp
        ../../../../../sdk-core/core/src/timeline/TemplateEngine.cpp
        ../../../../../sdk-core/core/src/timeline/SplitScreenCompositor.cpp
        ../../../../../sdk-core/core/src/timeline/SmartCropController.cpp
        ../../../../../sdk-core/core/src/timeline/HistogramAnalyzer.cpp
        ../../../../../sdk-core/core/src/timeline/ColorGradingFilter.cpp
        ../../../../../sdk-core/core/src/EffectPackageUpdater.cpp
        ../../../../../sdk-core/core/src/timeline/SmartCoverSelector.cpp
        ../../../../../sdk-core/core/src/timeline/DraftAutoSave.cpp
        ../../../../../sdk-core/core/src/audio/VoiceChangerFilter.cpp
        ../../../../../sdk-core/core/src/EffectPlugin.cpp
        ../../../../../sdk-core/core/src/rhi/GLTexture.cpp
        ../../../../../sdk-core/core/src/rhi/GLRenderDevice.cpp
        ../../../../../sdk-core/core/src/rhi/GLHardwareTexture.cpp
        ../../../../../sdk-core/core/src/rhi/GLBuffer.cpp
        ../../../../../sdk-core/core/src/rhi/GLVertexArray.cpp
        ../../../../../sdk-core/core/src/rhi/GLShaderProgram.cpp
        ../../../../../sdk-core/core/src/rhi/RenderDeviceFactory.cpp
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
    ../../../../../sdk-core/core/include
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
        ../../../../../sdk-core/core/src/rhi/vk/VulkanContext.cpp
        ../../../../../sdk-core/core/src/rhi/vk/VulkanMemoryAllocator.cpp
        ../../../../../sdk-core/core/src/rhi/vk/VulkanBuffer.cpp
        ../../../../../sdk-core/core/src/rhi/vk/VulkanTexture.cpp
        ../../../../../sdk-core/core/src/rhi/vk/VulkanShaderProgram.cpp
        ../../../../../sdk-core/core/src/rhi/vk/VulkanCommandBuffer.cpp
        ../../../../../sdk-core/core/src/rhi/vk/VulkanRenderDevice.cpp
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
