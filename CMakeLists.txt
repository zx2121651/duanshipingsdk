cmake_minimum_required(VERSION 3.10.2)
project("video-sdk-core")

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_SOURCE_DIR}/core/include)

set(CORE_SOURCES
    core/src/Filter.cpp
    core/src/Filters.cpp
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
)

# Core engine build
add_library(video_sdk_core STATIC ${CORE_SOURCES})

# Basic API logic test (no GL windowing dependencies)
add_executable(test_filter_graph tests/test_filter_graph.cpp tests/mocks/MockCompositor.cpp)
target_link_libraries(test_filter_graph video_sdk_core)

add_executable(test_execute_stability_repro tests/test_execute_stability_repro.cpp tests/mocks/MockCompositor.cpp)
if (NOT WIN32 AND NOT GLES2_LIBRARY)
    target_include_directories(test_execute_stability_repro PRIVATE ${CMAKE_SOURCE_DIR}/mock_gl)
endif()
target_link_libraries(test_execute_stability_repro video_sdk_core)

add_executable(test_lifecycle tests/test_lifecycle.cpp tests/mocks/MockCompositor.cpp)
if (NOT WIN32 AND NOT GLES2_LIBRARY)
    target_include_directories(test_lifecycle PRIVATE ${CMAKE_SOURCE_DIR}/mock_gl)
endif()
target_link_libraries(test_lifecycle video_sdk_core)

# If we are on Windows and using vcpkg, configure ANGLE (GLES implementation)
if(WIN32 AND DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
    find_package(unofficial-angle CONFIG REQUIRED)

    # Real rendering test that relies on EGL/GLES context (Replaced by SSIM assertion)
    add_executable(test_headless_ssim tests/test_headless_ssim.cpp tests/mocks/MockCompositor.cpp)
    target_link_libraries(test_headless_ssim
        video_sdk_core
        unofficial::angle::libGLESv2
        unofficial::angle::libEGL
    )

    # Since video_sdk_core uses gl* functions internally, it needs linking to GLES
    target_link_libraries(video_sdk_core unofficial::angle::libGLESv2)

    message(STATUS "Configured test_headless_ssim with ANGLE for Windows testing.")
else()
    # On Android NDK, linking is handled by Android Studio / ndk-build
    # On iOS/macOS, it's typically handled by Xcode frameworks.
    # For local Linux tests, we can find system GLESv2
    if (NOT WIN32)
        find_library(GLES2_LIBRARY GLESv2)
        if(GLES2_LIBRARY)
            target_link_libraries(video_sdk_core ${GLES2_LIBRARY})
            # Also build a mock render test if on linux and GLES/EGL are found
            find_library(EGL_LIBRARY EGL)
            if(EGL_LIBRARY)
                add_executable(test_headless_ssim tests/test_headless_ssim.cpp tests/mocks/MockCompositor.cpp)
                target_link_libraries(test_headless_ssim video_sdk_core ${GLES2_LIBRARY} ${EGL_LIBRARY})
            endif()
        else()
            message(WARNING "GLESv2 not found. Using mock GL headers for compilation.")
            target_include_directories(video_sdk_core PRIVATE ${CMAKE_SOURCE_DIR}/mock_gl)
            target_include_directories(test_filter_graph PRIVATE ${CMAKE_SOURCE_DIR}/mock_gl)
        endif()
    endif()
endif()

add_executable(test_timeline tests/test_timeline.cpp)
if (NOT WIN32 AND NOT GLES2_LIBRARY)
    target_include_directories(test_timeline PRIVATE ${CMAKE_SOURCE_DIR}/mock_gl)
endif()
target_link_libraries(test_timeline video_sdk_core)

add_executable(test_track_behavior tests/test_track_behavior.cpp)
if (NOT WIN32 AND NOT GLES2_LIBRARY)
    target_include_directories(test_track_behavior PRIVATE ${CMAKE_SOURCE_DIR}/mock_gl)
endif()
target_link_libraries(test_track_behavior video_sdk_core)

add_executable(test_rebuild_harden tests/test_rebuild_harden.cpp tests/mocks/MockCompositor.cpp)
if (NOT WIN32 AND NOT GLES2_LIBRARY)
    target_include_directories(test_rebuild_harden PRIVATE ${CMAKE_SOURCE_DIR}/mock_gl)
endif()
target_link_libraries(test_rebuild_harden video_sdk_core)
