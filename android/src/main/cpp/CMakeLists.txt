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
        ../../../../core/src/timeline/VideoDecoderAndroid.cpp
        ../../../../core/src/timeline/TimelineExporterAndroid.cpp
        ../../../../core/src/rhi/GLTexture.cpp
        ../../../../core/src/rhi/GLRenderDevice.cpp
)

add_library(video-sdk SHARED
    NativeBridge.cpp
    OboeAudioEngine.cpp
    TimelineBridge.cpp
    TimelineExporterBridge.cpp
    ${CORE_SRC}
)

# 包含路径
target_include_directories(video-sdk PRIVATE
    ../../../../core/include
    ${oboe_SOURCE_DIR}/include
)

# 链接依赖库
target_link_libraries(video-sdk
        android
        log
        EGL
        GLESv3
        oboe
        OpenSLES
        mediandk
)
