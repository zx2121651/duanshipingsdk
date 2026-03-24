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
)

# For unit tests, we'll build an executable
add_executable(test_filter_graph tests/test_filter_graph.cpp ${CORE_SOURCES})

# Assuming testing on a system with GLESv2 available
find_library(GLES2_LIBRARY GLESv2)
if(GLES2_LIBRARY)
    target_link_libraries(test_filter_graph ${GLES2_LIBRARY})
else()
    # Mocking or ignoring GL linkage if cross-compiling or running on CI without GL
    message(WARNING "GLESv2 not found. Tests might fail to link depending on environment.")
endif()
