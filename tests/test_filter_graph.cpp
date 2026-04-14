
#include <iostream>
#include <cassert>
#include <memory>
#include "../core/include/FilterEngine.h"
#include "../core/include/Filters.h"
#include "../core/include/pipeline/PipelineGraph.h"
#include "../core/include/pipeline/Nodes.h"

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

void test_graph_cycle_detection() {
    PipelineGraph graph;
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    auto nodeB = std::make_shared<OutputNode>("NodeB");

    graph.addNode(nodeA);
    graph.addNode(nodeB);
    graph.connect(nodeA, nodeB);
    graph.connect(nodeB, nodeA); // Cycle

    Result res = graph.compile();
    assert(!res.isOk());
    assert(res.getMessage().find("Cycle detected") != std::string::npos);

    std::cout << "test_graph_cycle_detection passed" << std::endl;
}

void test_graph_invalid_inputs() {
    PipelineGraph graph;
    // Null node adding
    assert(!graph.addNode(nullptr).isOk());

    // Null node connection
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    assert(!graph.connect(nullptr, nodeA).isOk());
    assert(!graph.connect(nodeA, nullptr).isOk());
    assert(!graph.connect(nullptr, nullptr).isOk());

    std::cout << "test_graph_invalid_inputs passed" << std::endl;
}

void test_graph_uncompiled_execution() {
    PipelineGraph graph;
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    auto nodeB = std::make_shared<OutputNode>("NodeB");
    graph.addNode(nodeA);
    graph.addNode(nodeB);
    graph.connect(nodeA, nodeB);

    // Execute without calling compile()
    Result res = graph.execute(1000);
    assert(!res.isOk());
    assert(res.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);

    std::cout << "test_graph_uncompiled_execution passed" << std::endl;
}

void test_graph_no_sink_nodes() {
    // This case is tricky because a DAG always has a sink.
    // However, if we have nodes but they only form cycles, they might be detected by cycle detection first.
    // Let's see if we can trigger the "No sink nodes found" error.

    PipelineGraph graph;
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    graph.addNode(nodeA);
    graph.connect(nodeA, nodeA); // Self-cycle

    Result res = graph.compile();
    assert(!res.isOk());
    // It should probably hit cycle detection first, but let's see.

    std::cout << "test_graph_no_sink_nodes (via cycle) passed" << std::endl;
}

int main() {
    test_filter_graph_creation();
    test_graph_cycle_detection();
    test_graph_invalid_inputs();
    test_graph_uncompiled_execution();
    test_graph_no_sink_nodes();
    return 0;
}
