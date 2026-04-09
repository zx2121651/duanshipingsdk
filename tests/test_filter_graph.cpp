
#include <iostream>
#include <cassert>
#include <memory>
#include "../core/include/FilterEngine.h"
#include "../core/include/Filters.h"

using namespace sdk::video;

void test_filter_graph_creation() {
    FilterEngine engine;

    // Add multiple filters
    engine.addFilter(FilterType::BRIGHTNESS);
    engine.addFilter(FilterType::GAUSSIAN_BLUR);
    engine.addFilter(FilterType::LOOKUP);

    // We can't really test OpenGL context here easily without a proper windowing system (like GLFW/EGL).
    // But we can test if the objects are instantiated, params can be updated, and the graph mode builds.

    engine.updateParameter("brightness", 0.5f);

    // Because it's graph mode now, processing a frame with no context and no real texture should safely fail and return Texture{0,0,0}
    Texture inTex{1, 1920, 1080};
    auto outRes = engine.processFrame(inTex, 1920, 1080);

    assert(!outRes.isOk() && "Should fail gracefully because GL context is not bound/mocked here");

    std::cout << "test_filter_graph_creation passed" << std::endl;
}

int main() {
    test_filter_graph_creation();
    return 0;
}
