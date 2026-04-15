
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
    assert(res.getErrorCode() == ErrorCode::ERR_GRAPH_CYCLE_DETECTED);
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
    PipelineGraph graph;
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    graph.addNode(nodeA);
    // nodeA has no outputs, so it IS a sink.
    // To have no sinks, we need all nodes to have at least one output, which implies a cycle in a finite graph.
    // But if we just have nodes and no connections? The current logic says if node->getOutputs().empty(), it's a sink.
    // So if we have nodeA with no outputs, it's a sink.

    // Wait, the only way to have no sink and not be empty is if every node has an output.
    // In a finite graph, that means there MUST be a cycle.

    // Let's try to bypass cycle detection and hit no sink?
    // Actually, compile() checks cycle for EACH node.
}

class FailureFilter : public Filter {
public:
    std::string getFragmentShaderName() const override { return "failure.frag"; }
    Result initialize() override {
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED, "Intentional failure");
    }
protected:
    void onDraw(const Texture&, FrameBufferPtr) override {}
    std::string getFragmentShaderSource() const override { return ""; }
};

void test_graph_node_init_failure() {
    PipelineGraph graph;
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    auto filter = std::make_shared<FailureFilter>();
    auto nodeB = std::make_shared<FilterNode>("NodeB", filter);
    auto nodeC = std::make_shared<OutputNode>("NodeC");

    graph.addNode(nodeA);
    graph.addNode(nodeB);
    graph.addNode(nodeC);
    graph.connect(nodeA, nodeB);
    graph.connect(nodeB, nodeC);

    Result res = graph.compile();
    assert(!res.isOk());
    assert(res.getErrorCode() == ErrorCode::ERR_GRAPH_NODE_INIT_FAILED);
    assert(res.getMessage().find("Intentional failure") != std::string::npos);

    // Ensure it's not compiled
    Result execRes = graph.execute(100);
    assert(!execRes.isOk());
    assert(execRes.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);

    std::cout << "test_graph_node_init_failure passed" << std::endl;
}

int main() {
    test_filter_graph_creation();
    test_graph_cycle_detection();
    test_graph_invalid_inputs();
    test_graph_uncompiled_execution();
    test_graph_node_init_failure();
    return 0;
}
