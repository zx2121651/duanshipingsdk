
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
    Result res = graph.addNode(nullptr);
    assert(!res.isOk());
    assert(res.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);
    assert(res.getMessage() == "Null node");

    // Null node connection
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    res = graph.connect(nullptr, nodeA);
    assert(!res.isOk());
    assert(res.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);
    assert(res.getMessage() == "Null node connect");

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
    // Case 1: Empty graph
    {
        PipelineGraph graph;
        Result res = graph.compile();
        assert(!res.isOk());
        assert(res.getErrorCode() == ErrorCode::ERR_GRAPH_NO_SINK);
        assert(res.getMessage().find("No nodes added") != std::string::npos);
    }

    // Case 2: Graph with nodes but no sinks (implies cycle)
    {
        PipelineGraph graph;
        auto nodeA = std::make_shared<CameraInputNode>("NodeA");
        auto nodeB = std::make_shared<OutputNode>("NodeB");

        graph.addNode(nodeA);
        graph.addNode(nodeB);
        graph.connect(nodeA, nodeB);
        graph.connect(nodeB, nodeA); // Cycle

        // Cycle detection happens before sink check
        Result res = graph.compile();
        assert(!res.isOk());
        assert(res.getErrorCode() == ErrorCode::ERR_GRAPH_CYCLE_DETECTED);
    }

    std::cout << "test_graph_no_sink_nodes passed" << std::endl;
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

class MockPullFailureNode : public PipelineNode {
public:
    MockPullFailureNode(const std::string& name) : PipelineNode(name) {}
    ResultPayload<VideoFrame> pullFrame(int64_t timestampNs) override {
        return ResultPayload<VideoFrame>::error(ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED, "Mock pull failure");
    }
};

void test_graph_execute_pull_failure() {
    PipelineGraph graph;
    auto node = std::make_shared<MockPullFailureNode>("FailureNode");
    graph.addNode(node);

    Result res = graph.compile();
    assert(res.isOk());

    Result execRes = graph.execute(1000);
    assert(!execRes.isOk());
    assert(execRes.getErrorCode() == ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED);
    assert(execRes.getMessage() == "Mock pull failure");

    std::cout << "test_graph_execute_pull_failure passed" << std::endl;
}

void test_graph_compile_idempotency() {
    PipelineGraph graph;
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    auto nodeB = std::make_shared<OutputNode>("NodeB");
    graph.addNode(nodeA);
    graph.addNode(nodeB);
    graph.connect(nodeA, nodeB);

    assert(graph.compile().isOk());

    // Modification 1: addNode
    auto nodeC = std::make_shared<CameraInputNode>("NodeC");
    graph.addNode(nodeC);

    Result execRes = graph.execute(100);
    assert(!execRes.isOk());
    assert(execRes.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);

    assert(graph.compile().isOk());

    // Modification 2: connect
    graph.connect(nodeC, nodeB);
    execRes = graph.execute(100);
    assert(!execRes.isOk());
    assert(execRes.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);

    assert(graph.compile().isOk());

    // Push a frame to make execution successful
    VideoFrame frame;
    frame.textureId = 1; frame.width = 100; frame.height = 100;
    nodeA->pushFrame(frame);
    nodeC->pushFrame(frame);

    assert(graph.execute(100).isOk());

    std::cout << "test_graph_compile_idempotency passed" << std::endl;
}

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
    test_graph_no_sink_nodes();
    test_graph_execute_pull_failure();
    test_graph_compile_idempotency();
    return 0;
}
