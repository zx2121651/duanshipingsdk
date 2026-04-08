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



)

# Core engine build
add_library(video_sdk_core STATIC ${CORE_SOURCES})

# Basic API logic test (no GL windowing dependencies)
add_executable(test_filter_graph tests/test_filter_graph.cpp)
target_link_libraries(test_filter_graph video_sdk_core)

# If we are on Windows and using vcpkg, configure ANGLE (GLES implementation)
if(WIN32 AND DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
    find_package(unofficial-angle CONFIG REQUIRED)

    # Real rendering test that relies on EGL/GLES context
    add_executable(test_render_angle tests/test_render_angle.cpp)
    target_link_libraries(test_render_angle
        video_sdk_core
        unofficial::angle::libGLESv2
        unofficial::angle::libEGL
    )

    # Since video_sdk_core uses gl* functions internally, it needs linking to GLES
    target_link_libraries(video_sdk_core unofficial::angle::libGLESv2)

    message(STATUS "Configured test_render_angle with ANGLE for Windows testing.")
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
                add_executable(test_render_angle tests/test_render_angle.cpp)
                target_link_libraries(test_render_angle video_sdk_core ${GLES2_LIBRARY} ${EGL_LIBRARY})
            endif()
        else()
            message(WARNING "GLESv2 not found. Local tests might fail to link depending on environment.")
        endif()
    endif()
endif()
