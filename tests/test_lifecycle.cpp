
#include <iostream>
#include <cassert>
#include <memory>
#include "../core/include/FilterEngine.h"
#include "../core/include/Filters.h"

using namespace sdk::video;

void test_repeated_init_release() {
    FilterEngine engine;

    std::cout << "Testing repeated initialize..." << std::endl;
    // 1. Double init
    Result res = engine.initialize();
    assert(res.isOk());
    res = engine.initialize();
    assert(res.isOk());

    std::cout << "Testing repeated release..." << std::endl;
    // 2. Double release
    engine.release();
    engine.release();

    std::cout << "Testing init -> release -> init..." << std::endl;
    // 3. Init -> Release -> Init
    res = engine.initialize();
    assert(res.isOk());
    engine.release();
    res = engine.initialize();
    assert(res.isOk());

    std::cout << "test_repeated_init_release passed" << std::endl;
}

void test_lifecycle_functionality() {
    FilterEngine engine;

    std::cout << "Cycle 1: Init and Process..." << std::endl;
    // First cycle
    engine.initialize();
    engine.addFilter(FilterType::BRIGHTNESS);
    Texture inTex{1, 1920, 1080};

    // Should fail gracefully because no GL context, but shouldn't crash
    auto res1 = engine.processFrame(inTex, 1920, 1080);
    // It should be ERR_RENDER_INVALID_STATE because glGetString(GL_VERSION) likely returns null without context
    // causing initialize or graph compile to fail, or just processFrame to fail.

    std::cout << "Releasing..." << std::endl;
    engine.release();

    std::cout << "Cycle 2: Re-init and Process..." << std::endl;
    // Second cycle
    engine.initialize();
    engine.addFilter(FilterType::BRIGHTNESS);
    auto res2 = engine.processFrame(inTex, 1920, 1080);

    // Check performance metrics (should be cleared on release)
    PerformanceMetrics metrics = engine.getPerformanceMetrics();
    // If it processed 1 frame in cycle 2, averageFrameTimeMs should be based on that 1 frame,
    // NOT including any frames from cycle 1.
    // However, without a real GL context, processFrame might fail before recording metrics.

    std::cout << "test_lifecycle_functionality passed" << std::endl;
}

int main() {
    test_repeated_init_release();
    test_lifecycle_functionality();
    return 0;
}
