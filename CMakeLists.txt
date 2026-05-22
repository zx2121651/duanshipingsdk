cmake_minimum_required(VERSION 3.10.2)
project("video-sdk-core")

set(CMAKE_CXX_STANDARD 17)

# MSVC: treat all source files as UTF-8 to avoid C4819 / parse failures
# caused by Chinese comments in headers (GLStateManager.h etc.)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# --- GLES / Mock GL Strategy ---
# By default, we try to find a real GLESv2 implementation.
# If not found (e.g., in pure Linux CI or non-vcpkg Windows), we fallback to mock_gl.
# You can force mock_gl with -DUSE_MOCK_GL=ON.
option(USE_MOCK_GL "Use mock GLES headers for non-GPU environments" OFF)

if(NOT USE_MOCK_GL)
    if(WIN32)
        if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
            find_package(unofficial-angle CONFIG QUIET)
            if(NOT unofficial-angle_FOUND)
                set(USE_MOCK_GL ON)
            endif()
        else()
            set(USE_MOCK_GL ON)
        endif()
    else()
        find_library(GLES2_LIBRARY GLESv2)
        if(NOT GLES2_LIBRARY)
            set(USE_MOCK_GL ON)
        endif()
    endif()
endif()

if(USE_MOCK_GL)
    message(STATUS "Build Strategy: Using MOCK_GL (Headless/No-GPU)")
else()
    message(STATUS "Build Strategy: Using Real GLESv2")
endif()

include_directories(${CMAKE_SOURCE_DIR}/core/include)

# ---------------------------------------------------------------------------
# FFmpeg 软解支持（可选）
# 若系统中存在 libavcodec，则编译 FFmpegVideoDecoder 并定义 HAS_FFMPEG_DECODER。
# Android 侧通过 android/src/main/cpp/CMakeLists.txt 的显式路径配置。
# ---------------------------------------------------------------------------
option(WITH_FFMPEG "Enable FFmpeg software decoder" OFF)

# ---------------------------------------------------------------------------
# TensorFlow Lite 推理引擎（可选）
# 用于 AI 美颜/人像分割。Android 侧通过 TFLITE_PREBUILT_DIR 传入预编译库。
# ---------------------------------------------------------------------------
option(WITH_TFLITE  "Enable TensorFlow Lite inference engine" OFF)
option(WITH_VULKAN  "Enable Vulkan RHI backend (Android NDK / desktop Vulkan SDK)" OFF)
option(WITH_METAL   "Enable Metal RHI backend (iOS / macOS only)" OFF)

# ---------------------------------------------------------------------------
# Vulkan 后端检测
# ---------------------------------------------------------------------------
if(WITH_VULKAN)
    if(ANDROID)
        # Android NDK 自带 Vulkan 头文件，无需 find_package
        set(HAS_VULKAN ON)
        message(STATUS "Vulkan: Android NDK built-in")
    else()
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND)
            set(HAS_VULKAN ON)
            message(STATUS "Vulkan found: ${Vulkan_LIBRARIES}")
        else()
            message(STATUS "Vulkan NOT found — VulkanRenderDevice will be excluded")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Metal 后端检测（仅 Apple 平台）
# ---------------------------------------------------------------------------
if(WITH_METAL AND APPLE)
    set(HAS_METAL ON)
    message(STATUS "Metal: enabled (Apple platform)")
elseif(WITH_METAL)
    message(STATUS "Metal: skipped (non-Apple platform)")
endif()

# ---------------------------------------------------------------------------
# SPIR-V 编译函数（需要 glslangValidator 在 PATH 中）
# ---------------------------------------------------------------------------
function(compile_spirv TARGET SHADER_FILE)
    get_filename_component(BASE ${SHADER_FILE} NAME_WE)
    get_filename_component(EXT  ${SHADER_FILE} EXT)
    string(REPLACE "." "_" STAGE_ID "${EXT}")
    set(OUTPUT_FILE "${CMAKE_BINARY_DIR}/spirv/${BASE}${STAGE_ID}.spv")
    add_custom_command(
        OUTPUT  ${OUTPUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/spirv"
        COMMAND glslangValidator -V -o ${OUTPUT_FILE} ${SHADER_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling SPIR-V: ${SHADER_FILE}"
    )
    target_sources(${TARGET} PRIVATE ${OUTPUT_FILE})
endfunction()

if(WITH_TFLITE OR DEFINED TFLITE_ROOT)
    find_path(TFLITE_INCLUDE_DIR tensorflow/lite/interpreter.h
              HINTS ${TFLITE_ROOT}/include ENV TFLITE_ROOT)
    find_library(TFLITE_LIB tensorflowlite
                 HINTS ${TFLITE_ROOT}/lib ENV TFLITE_ROOT)
    if(TFLITE_LIB AND TFLITE_INCLUDE_DIR)
        message(STATUS "TFLite found — enabling HAS_TFLITE")
        set(HAS_TFLITE ON)
    else()
        message(STATUS "TFLite NOT found — AI inference will use stub")
    endif()
endif()

if(WITH_FFMPEG OR DEFINED FFMPEG_ROOT)
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
              HINTS ${FFMPEG_ROOT}/include ENV FFMPEG_ROOT)
    find_library(FFMPEG_AVCODEC_LIB   avcodec   HINTS ${FFMPEG_ROOT}/lib ENV FFMPEG_ROOT)
    find_library(FFMPEG_AVFORMAT_LIB  avformat  HINTS ${FFMPEG_ROOT}/lib ENV FFMPEG_ROOT)
    find_library(FFMPEG_AVUTIL_LIB    avutil    HINTS ${FFMPEG_ROOT}/lib ENV FFMPEG_ROOT)
    find_library(FFMPEG_SWSCALE_LIB   swscale   HINTS ${FFMPEG_ROOT}/lib ENV FFMPEG_ROOT)

    if(FFMPEG_AVCODEC_LIB AND FFMPEG_AVFORMAT_LIB AND FFMPEG_AVUTIL_LIB AND FFMPEG_SWSCALE_LIB)
        message(STATUS "FFmpeg found — enabling HAS_FFMPEG_DECODER")
        set(HAS_FFMPEG ON)
    else()
        message(STATUS "FFmpeg NOT found — SoftwareVideoDecoder will remain a stub")
    endif()
endif()

set(CORE_SOURCES
    core/src/Filter.cpp
    core/src/Filters.cpp
    core/src/PropOverlayFilter.cpp
    core/src/FilterEngine.cpp
    core/src/FrameBuffer.cpp
    core/src/FrameBufferPool.cpp
    core/src/ShaderManager.cpp
    core/src/pipeline/PipelineGraph.cpp
    core/src/ThreadCheck.cpp
    core/src/GLContextManager.cpp
    core/src/GLStateManager.cpp
    core/src/timeline/Timeline.cpp
    core/src/timeline/Track.cpp
    core/src/timeline/Clip.cpp
    core/src/timeline/Compositor.cpp
    core/src/timeline/DecoderPool.cpp
    core/src/timeline/AudioMixer.cpp
    core/src/timeline/AudioEffects.cpp
    core/src/rhi/GLTexture.cpp
    core/src/rhi/GLRenderDevice.cpp
    core/src/rhi/GLHardwareTexture.cpp
    core/src/rhi/GLBuffer.cpp
    core/src/rhi/GLVertexArray.cpp
    core/src/rhi/GLShaderProgram.cpp
    core/src/rhi/RenderDeviceFactory.cpp
    core/src/ai/TfliteInferenceEngine.cpp
    core/src/ai/SegmentationFilter.cpp
    core/src/ai/BeautyFilter.cpp
    core/src/ai/InferenceEngine.cpp
    core/src/ai/FaceLandmarkDetector.cpp
    core/src/ai/FaceReshapeFilter.cpp
    core/src/ai/MakeupFilter.cpp
    core/src/ai/HairSegmentationFilter.cpp
    core/src/ai/BodyPoseDetector.cpp
    core/src/ai/GestureRecognizer.cpp
    core/src/ai/StickerRenderer.cpp
    core/src/ai/HandLandmarkDetector.cpp
    core/src/ai/AutoSubtitleEngine.cpp
    core/src/ai/ChromaKeyFilter.cpp
    core/src/ai/ExpressionDetector.cpp
    core/src/ai/VideoStabilizer.cpp
    core/src/ai/ArFaceAnchor.cpp
    core/src/ai/ParticleSystem.cpp
    core/src/ai/ObjectTracker.cpp
    core/src/ai/FaceMorphFilter.cpp
    core/src/ai/BodyEffectFilter.cpp
    core/src/ai/DepthEstimator.cpp
    core/src/timeline/TemplateEngine.cpp
    core/src/timeline/SplitScreenCompositor.cpp
    core/src/timeline/SmartCropController.cpp
    core/src/timeline/HistogramAnalyzer.cpp
    core/src/timeline/ColorGradingFilter.cpp
    core/src/EffectPackageUpdater.cpp
    core/src/timeline/SmartCoverSelector.cpp
    core/src/timeline/DraftAutoSave.cpp
    core/src/audio/VoiceChangerFilter.cpp
    core/src/EffectPlugin.cpp
)

# Phase-1 Verification Target: Core engine build
add_library(video_sdk_core STATIC ${CORE_SOURCES})

# Vulkan 后端条件编译
if(HAS_VULKAN)
    target_compile_definitions(video_sdk_core PUBLIC HAS_VULKAN)
    target_sources(video_sdk_core PRIVATE
        core/src/rhi/vk/VulkanContext.cpp
        core/src/rhi/vk/VulkanMemoryAllocator.cpp
        core/src/rhi/vk/VulkanBuffer.cpp
        core/src/rhi/vk/VulkanTexture.cpp
        core/src/rhi/vk/VulkanShaderProgram.cpp
        core/src/rhi/vk/VulkanCommandBuffer.cpp
        core/src/rhi/vk/VulkanRenderDevice.cpp
    )
    if(Vulkan_FOUND)
        target_include_directories(video_sdk_core PUBLIC ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(video_sdk_core ${Vulkan_LIBRARIES})
    elseif(ANDROID)
        target_link_libraries(video_sdk_core vulkan)
    endif()
    message(STATUS "Vulkan RHI backend: ENABLED")
endif()

# Metal 后端条件编译
if(HAS_METAL)
    target_compile_definitions(video_sdk_core PUBLIC HAS_METAL)
    target_sources(video_sdk_core PRIVATE
        core/src/rhi/mtl/MetalTexture.mm
        core/src/rhi/mtl/MetalBuffer.mm
        core/src/rhi/mtl/MetalShaderProgram.mm
        core/src/rhi/mtl/MetalCommandBuffer.mm
        core/src/rhi/mtl/MetalRenderDevice.mm
    )
    target_link_libraries(video_sdk_core
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
    )
    message(STATUS "Metal RHI backend: ENABLED")
endif()

# TFLite 条件链接
if(HAS_TFLITE)
    target_compile_definitions(video_sdk_core PUBLIC HAS_TFLITE)
    target_include_directories(video_sdk_core PUBLIC ${TFLITE_INCLUDE_DIR})
    target_link_libraries(video_sdk_core ${TFLITE_LIB})
endif()

# FFmpeg 条件链接
if(HAS_FFMPEG)
    target_compile_definitions(video_sdk_core PUBLIC HAS_FFMPEG_DECODER)
    target_sources(video_sdk_core PRIVATE core/src/timeline/FFmpegVideoDecoder.cpp)
    target_include_directories(video_sdk_core PUBLIC ${FFMPEG_INCLUDE_DIR})
    target_link_libraries(video_sdk_core
        ${FFMPEG_AVCODEC_LIB}
        ${FFMPEG_AVFORMAT_LIB}
        ${FFMPEG_AVUTIL_LIB}
        ${FFMPEG_SWSCALE_LIB}
    )
endif()

if(USE_MOCK_GL)
    target_compile_definitions(video_sdk_core PUBLIC USE_MOCK_GL)
    target_include_directories(video_sdk_core PUBLIC ${CMAKE_SOURCE_DIR}/mock_gl)
endif()

enable_testing()

# Phase-1 Verification: Core Unit Tests
# Basic API logic test (no GL windowing dependencies)
add_executable(test_filter_graph tests/test_filter_graph.cpp tests/mocks/MockCompositor.cpp)
add_test(NAME test_filter_graph COMMAND test_filter_graph)

target_link_libraries(test_filter_graph video_sdk_core)

add_executable(test_execute_stability_repro tests/test_execute_stability_repro.cpp tests/mocks/MockCompositor.cpp)
target_link_libraries(test_execute_stability_repro video_sdk_core)
add_test(NAME test_execute_stability_repro COMMAND test_execute_stability_repro)

add_executable(test_lifecycle tests/test_lifecycle.cpp tests/mocks/MockCompositor.cpp)
target_link_libraries(test_lifecycle video_sdk_core)
add_test(NAME test_lifecycle COMMAND test_lifecycle)

# --- Real Rendering Tests / Linking ---
if(NOT USE_MOCK_GL)
    if(WIN32)
        # Using ANGLE on Windows (configured via vcpkg)
        find_package(unofficial-angle CONFIG REQUIRED)
        target_link_libraries(video_sdk_core unofficial::angle::libGLESv2)

        add_executable(test_headless_ssim tests/test_headless_ssim.cpp tests/mocks/MockCompositor.cpp)
        target_link_libraries(test_headless_ssim
            video_sdk_core
            unofficial::angle::libGLESv2
            unofficial::angle::libEGL
        )
    else()
        # On Linux/Other, we found GLES2_LIBRARY earlier
        target_link_libraries(video_sdk_core ${GLES2_LIBRARY})

        find_library(EGL_LIBRARY EGL)
        if(EGL_LIBRARY)
            add_executable(test_headless_ssim tests/test_headless_ssim.cpp tests/mocks/MockCompositor.cpp)
            target_link_libraries(test_headless_ssim video_sdk_core ${GLES2_LIBRARY} ${EGL_LIBRARY})
        endif()
    endif()
endif()

add_executable(test_timeline tests/test_timeline.cpp)
target_link_libraries(test_timeline video_sdk_core)
add_test(NAME test_timeline COMMAND test_timeline)

add_executable(test_decoder_pool tests/test_decoder_pool.cpp)
target_link_libraries(test_decoder_pool video_sdk_core)
add_test(NAME test_decoder_pool COMMAND test_decoder_pool)

add_executable(test_track_behavior tests/test_track_behavior.cpp)
target_link_libraries(test_track_behavior video_sdk_core)
add_test(NAME test_track_behavior COMMAND test_track_behavior)

add_executable(test_rebuild_harden tests/test_rebuild_harden.cpp tests/mocks/MockCompositor.cpp)
target_link_libraries(test_rebuild_harden video_sdk_core)
add_test(NAME test_rebuild_harden COMMAND test_rebuild_harden)

add_executable(test_timeline_node_harden tests/test_timeline_node_harden.cpp tests/mocks/MockCompositor.cpp)
target_link_libraries(test_timeline_node_harden video_sdk_core)
add_test(NAME test_timeline_node_harden COMMAND test_timeline_node_harden)

add_executable(test_exporter_logic tests/test_exporter_logic.cpp tests/mocks/MockCompositor.cpp)
target_link_libraries(test_exporter_logic video_sdk_core)
add_test(NAME test_exporter_logic COMMAND test_exporter_logic)

add_executable(test_ffmpeg_decoder tests/test_ffmpeg_decoder.cpp)
target_link_libraries(test_ffmpeg_decoder video_sdk_core)
add_test(NAME test_ffmpeg_decoder COMMAND test_ffmpeg_decoder)

add_executable(test_ai_inference tests/test_ai_inference.cpp)
target_link_libraries(test_ai_inference video_sdk_core)
add_test(NAME test_ai_inference COMMAND test_ai_inference)

add_executable(test_rhi_backend tests/test_rhi_backend.cpp)
target_link_libraries(test_rhi_backend video_sdk_core)
add_test(NAME test_rhi_backend COMMAND test_rhi_backend)

add_executable(test_douyin_compat tests/test_douyin_compat.cpp)
target_link_libraries(test_douyin_compat video_sdk_core)
add_test(NAME test_douyin_compat COMMAND test_douyin_compat)

# Phase B — 导出链路 E2E 测试
add_executable(test_export_e2e tests/test_export_e2e.cpp tests/mocks/MockCompositor.cpp)
target_link_libraries(test_export_e2e video_sdk_core)
add_test(NAME test_export_e2e COMMAND test_export_e2e)

# Phase C — 素材兼容测试套件
add_executable(test_media_compat tests/test_media_compat.cpp)
target_link_libraries(test_media_compat video_sdk_core)
add_test(NAME test_media_compat COMMAND test_media_compat)

# Phase E — Compute Blur 性能基准
add_executable(test_compute_blur_perf tests/test_compute_blur_perf.cpp)
target_link_libraries(test_compute_blur_perf video_sdk_core)
add_test(NAME test_compute_blur_perf COMMAND test_compute_blur_perf)

# Phase 1 Gap — HandLandmarkDetector / AutoSubtitleEngine / TemplateEngine / MediaType
add_executable(test_phase1_gaps tests/test_phase1_gaps.cpp)
target_link_libraries(test_phase1_gaps video_sdk_core)
add_test(NAME test_phase1_gaps COMMAND test_phase1_gaps)

# Phase 2 Gap — ChromaKeyFilter / BlendMode / VoiceChangerFilter / ExpressionDetector /
#               VideoStabilizer / ExportPreset / SmartCoverSelector / DraftAutoSave
add_executable(test_phase2_gaps tests/test_phase2_gaps.cpp)
target_link_libraries(test_phase2_gaps video_sdk_core)
add_test(NAME test_phase2_gaps COMMAND test_phase2_gaps)

# Phase 3 Gap — EffectPackageUpdater / ArFaceAnchor / ParticleSystem /
#               ColorGradingFilter / ObjectTracker
add_executable(test_phase3_gaps tests/test_phase3_gaps.cpp)
target_link_libraries(test_phase3_gaps video_sdk_core)
add_test(NAME test_phase3_gaps COMMAND test_phase3_gaps)

# Remaining gaps — FaceMorphFilter / BodyEffectFilter / SplitScreenCompositor /
#                  SmartCropController / HistogramAnalyzer / DepthEstimator
add_executable(test_remaining_gaps tests/test_remaining_gaps.cpp)
target_link_libraries(test_remaining_gaps video_sdk_core)
add_test(NAME test_remaining_gaps COMMAND test_remaining_gaps)
