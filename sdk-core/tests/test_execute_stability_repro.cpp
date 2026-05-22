
#include <iostream>
#include <cassert>
#include <memory>
#include "../core/include/pipeline/PipelineGraph.h"
#include "../core/include/pipeline/Nodes.h"

using namespace sdk::video;

class MockFailureFilter : public Filter {
public:
    Result initialize() override {
        return Result::error(-1, "Forced Init Failure");
    }
protected:
    void onDraw(const Texture&, FrameBufferPtr) override {}
    std::string getFragmentShaderSource() const override { return ""; }
    std::string getFragmentShaderName() const override { return "mock"; }
};

void test_detach_nodes_resets_compiled() {
    PipelineGraph graph;
    auto node = std::make_shared<CameraInputNode>("input");
    graph.addNode(node);
    graph.compile();

    // Check if it's compiled (internal state check via execute result)
    // Result res = graph.execute(0); // If it was NOT compiled, it would return ERR_RENDER_INVALID_STATE
    // assert(res.getErrorCode() != ErrorCode::ERR_RENDER_INVALID_STATE);

    graph.detachAllNodes();

    // BUG: detachAllNodes() should reset m_isCompiled.
    // If it doesn't, execute might try to run on cleared vectors.
    Result res2 = graph.execute(0);
    assert(res2.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);
}

void test_empty_graph_compile() {
    PipelineGraph graph;
    Result res = graph.compile();
    // Empty graph compile() should fail
    assert(!res.isOk());
    assert(res.getErrorCode() == ErrorCode::ERR_GRAPH_NO_SINK);
}

void test_compile_failure_resets_state() {
    PipelineGraph graph;
    auto nodeA = std::make_shared<CameraInputNode>("NodeA");
    auto failFilter = std::make_shared<MockFailureFilter>();
    auto nodeB = std::make_shared<FilterNode>("NodeB", failFilter);
    auto nodeC = std::make_shared<OutputNode>("NodeC");

    graph.addNode(nodeA);
    graph.addNode(nodeB);
    graph.addNode(nodeC);
    graph.connect(nodeA, nodeB);
    graph.connect(nodeB, nodeC);

    // First, make it valid so it compiles
    // (Wait, I can't easily swap the filter. I'll just try to compile it once and it should fail)

    Result res = graph.compile();
    assert(!res.isOk());

    // BUG: If compile failed, m_isCompiled might still be whatever it was before (or false if it's new).
    // But we want to ensure it's DEFINITELY false.

    Result execRes = graph.execute(0);
    assert(execRes.getErrorCode() == ErrorCode::ERR_RENDER_INVALID_STATE);
}

int main() {
    std::cout << "Running stability repro tests..." << std::endl;
    test_detach_nodes_resets_compiled();
    test_empty_graph_compile();
    test_compile_failure_resets_state();
    return 0;
}
