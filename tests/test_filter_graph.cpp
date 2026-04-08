#include "../core/include/timeline/Compositor.h"

namespace sdk {
namespace video {
namespace timeline {
    Result Compositor::renderFrameAtTime(int64_t timelineUs, FrameBufferPtr outputFb) {
        return Result::ok();
    }
}
}
}

#include <iostream>
#include <cassert>
#include <memory>
#include "../core/include/FilterEngine.h"
#include "../core/include/Filters.h"

using namespace sdk::video;

void test_filter_graph_creation() {
    FilterEngine engine;

    // Add multiple filters
    engine.addFilter(std::make_shared<BrightnessFilter>());
    engine.addFilter(std::make_shared<GaussianBlurFilter>(&(engine.m_frameBufferPool)));
    engine.addFilter(std::make_shared<LookupFilter>());

    // We can't really test OpenGL context here easily without a proper windowing system (like GLFW/EGL).
    // But we can test if the objects are instantiated, params can be updated, and the graph mode builds.

    engine.updateParameter("brightness", 0.5f);

    // Because it's graph mode now, processing a frame with no context and no real texture should safely fail and return Texture{0,0,0}
    Texture inTex{1, 1920, 1080};
    Texture outTex = engine.processFrame(inTex, 1920, 1080);

    assert(outTex.id == 0 && "Output texture should be 0 because GL context is not mocked here and it will fail gracefully");

    std::cout << "test_filter_graph_creation passed" << std::endl;
}

int main() {
    test_filter_graph_creation();
    return 0;
}
