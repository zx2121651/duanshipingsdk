cmake_minimum_required(VERSION 3.10.2)
project("video-sdk-core")

set(CMAKE_CXX_STANDARD 17)

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
    core/src/rhi/GLTexture.cpp
    core/src/rhi/GLRenderDevice.cpp
)

# Core engine build
add_library(video_sdk_core STATIC ${CORE_SOURCES})

if(USE_MOCK_GL)
    target_include_directories(video_sdk_core PUBLIC ${CMAKE_SOURCE_DIR}/mock_gl)
endif()

enable_testing()

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
