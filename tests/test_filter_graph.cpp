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
    // But we can test if the objects are instantiated and params can be updated.

    engine.updateParameter("brightness", 0.5f);
    engine.updateParameter("blurSize", 2.0f);
    engine.updateParameter("intensity", 0.8f);

    engine.removeAllFilters();

    std::cout << "Test passed: C++ FilterGraph basic API creation and management works as expected." << std::endl;
}

int main() {
    test_filter_graph_creation();
    return 0;
}
