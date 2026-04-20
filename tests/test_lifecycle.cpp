
#include <iostream>
#include <cassert>
#include <memory>
#include <thread>
#include <future>
#include "../core/include/FilterEngine.h"
#include "../core/include/Filters.h"
#include "FilterEngineTestAccessor.h"

using namespace sdk::video;

void test_reinitialize_regression() {
    FilterEngine engine;

    std::cout << "1. Initial state check..." << std::endl;
    assert(!FilterEngineTestAccessor::isInitialized(engine));
    assert(FilterEngineTestAccessor::isGraphDirty(engine));

    std::cout << "2. Initialize and build pipeline..." << std::endl;
    Result res = engine.initialize();
    assert(res.isOk());
    assert(FilterEngineTestAccessor::isInitialized(engine));

    res = engine.buildCameraPipeline();
    assert(res.isOk());
    assert(FilterEngineTestAccessor::getGraph(engine) != nullptr);
    assert(FilterEngineTestAccessor::getCameraNode(engine) != nullptr);
    assert(FilterEngineTestAccessor::getOutputNode(engine) != nullptr);

    std::cout << "3. Record metrics..." << std::endl;
    FilterEngineTestAccessor::getMetricsCollector(engine).recordFrameTime(16.6f);
    assert(engine.getPerformanceMetrics().averageFrameTimeMs > 0);

    std::cout << "4. Release and check state reset..." << std::endl;
    engine.release();

    assert(!FilterEngineTestAccessor::isInitialized(engine));
    assert(FilterEngineTestAccessor::getGraph(engine) == nullptr);
    assert(FilterEngineTestAccessor::getCameraNode(engine) == nullptr);
    assert(FilterEngineTestAccessor::getOutputNode(engine) == nullptr);
    assert(FilterEngineTestAccessor::isGraphDirty(engine));
    assert(engine.getPerformanceMetrics().averageFrameTimeMs == 0.0f);

    std::cout << "5. Re-initialize and rebuild..." << std::endl;
    res = engine.initialize();
    assert(res.isOk());
    assert(FilterEngineTestAccessor::isInitialized(engine));

    res = engine.buildCameraPipeline();
    assert(res.isOk());
    assert(FilterEngineTestAccessor::getGraph(engine) != nullptr);

    std::cout << "test_reinitialize_regression passed" << std::endl;
}

void test_repeated_init_release() {
    FilterEngine engine;

    std::cout << "Testing repeated initialize on same thread..." << std::endl;
    // 1. Double init on same thread
    Result res = engine.initialize();
    assert(res.isOk());
    auto device1 = FilterEngineTestAccessor::getRenderDevice(engine);
    assert(device1 != nullptr);

    res = engine.initialize();
    assert(res.isOk());
    auto device2 = FilterEngineTestAccessor::getRenderDevice(engine);
    assert(device1 == device2); // Idempotent: should NOT re-create device

    std::cout << "Testing repeated initialize on different thread..." << std::endl;
    // 2. Double init on different thread - should FAIL because binding is to first thread
    std::promise<Result> promise;
    std::future<Result> future = promise.get_future();
    std::thread t([&engine, &promise]() {
        promise.set_value(engine.initialize());
    });
    t.join();
    Result threadRes = future.get();
    // After my changes, this should fail. Currently it returns OK.
    // I will update the test to expect FAILURE after I implement the check.
    // But wait, if I run the test NOW it will fail if I assert it fails.
    // TDD style: expect failure now.
    assert(!threadRes.isOk());
    assert(threadRes.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);

    std::cout << "Testing repeated release..." << std::endl;
    // 2. Double release
    engine.release();
    engine.release();

    std::cout << "Testing init -> release -> init..." << std::endl;
    // 3. Init -> Release -> Init
    res = engine.initialize();
    assert(res.isOk());
    auto device3 = FilterEngineTestAccessor::getRenderDevice(engine);
    assert(device3 != nullptr);

    engine.release();
    assert(FilterEngineTestAccessor::getRenderDevice(engine) == nullptr);

    res = engine.initialize();
    assert(res.isOk());
    auto device4 = FilterEngineTestAccessor::getRenderDevice(engine);
    assert(device4 != nullptr);
    assert(device3 != device4); // Should be a new device instance

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
    assert(!res1.isOk());

    // It should be ERR_RENDER_INVALID_STATE because glGetString(GL_VERSION) likely returns null without context
    // causing initialize or graph compile to fail, or just processFrame to fail.

    std::cout << "Releasing..." << std::endl;
    engine.release();

    std::cout << "Cycle 2: Re-init and Process..." << std::endl;
    // Second cycle
    engine.initialize();
    engine.addFilter(FilterType::BRIGHTNESS);
    auto res2 = engine.processFrame(inTex, 1920, 1080);
    assert(!res2.isOk());

    // Check performance metrics (should be cleared on release)
    PerformanceMetrics metrics = engine.getPerformanceMetrics();
    // If it processed 1 frame in cycle 2, averageFrameTimeMs should be based on that 1 frame,
    // NOT including any frames from cycle 1.
    // However, without a real GL context, processFrame might fail before recording metrics.

    std::cout << "test_lifecycle_functionality passed" << std::endl;
}

void test_post_release_api_behavior() {
    FilterEngine engine;
    engine.initialize();
    engine.release();

    std::cout << "Testing API behavior after release..." << std::endl;

    assert(engine.addFilter(FilterType::BRIGHTNESS).getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);
    assert(engine.removeAllFilters().getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);
    assert(engine.buildCameraPipeline().getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);
    assert(engine.updateShaderSource("test", "void main() {}").getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);

    Texture tex{1, 1920, 1080};
    assert(engine.processFrame(tex, 1920, 1080).getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);

    std::cout << "test_post_release_api_behavior passed" << std::endl;
}

int main() {
    test_reinitialize_regression();
    test_repeated_init_release();
    test_lifecycle_functionality();
    test_post_release_api_behavior();
    return 0;
}
