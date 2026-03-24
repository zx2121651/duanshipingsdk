cmake_minimum_required(VERSION 3.10.2)
project("video-sdk")

set(CMAKE_CXX_STANDARD 17)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/../../../core/include
)

# Source files
set(CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/../../../core/src/Filter.cpp
    ${CMAKE_SOURCE_DIR}/../../../core/src/Filters.cpp
    ${CMAKE_SOURCE_DIR}/../../../core/src/FilterEngine.cpp
    ${CMAKE_SOURCE_DIR}/../../../core/src/FrameBuffer.cpp
    ${CMAKE_SOURCE_DIR}/../../../core/src/FrameBufferPool.cpp
)

set(JNI_SOURCES
    ${CMAKE_SOURCE_DIR}/NativeBridge.cpp
)

add_library(
    video-sdk
    SHARED
    ${CORE_SOURCES}
    ${JNI_SOURCES}
)

find_library(
    log-lib
    log
)

find_library(
    gles3-lib
    GLESv3
)

target_link_libraries(
    video-sdk
    ${log-lib}
    ${gles3-lib}
)
