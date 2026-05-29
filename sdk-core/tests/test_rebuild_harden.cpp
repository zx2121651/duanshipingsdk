
#include <iostream>
#include <cassert>
#include <memory>
#include <algorithm>
#include "../core/include/FilterEngine.h"
#include "../core/include/pipeline/Nodes.h"
#include "FilterEngineTestAccessor.h"

using namespace sdk::video;

class ToggledFailureFilter : public Filter {
public:
    bool shouldFail = false;
    std::string getFragmentShaderName() const override { return "toggled.frag"; }
    Result initialize() override {
        if (shouldFail) {
            return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED, "Intentional failure");
        }
        return Result::ok();
    }
protected:
    void onDraw(const Texture&, FrameBufferPtr) override {}
    std::string getFragmentShaderSource() const override { return ""; }
};

void test_rebuild_success() {
    FilterEngine engine;
    engine.initialize();

    auto filter1 = std::make_shared<ToggledFailureFilter>();
    auto filter2 = std::make_shared<ToggledFailureFilter>();
    engine.addFilterRaw(filter1);
    engine.addFilterRaw(filter2);

    Result res = engine.buildCameraPipeline();
    assert(res.isOk());

    auto graph = FilterEngineTestAccessor::getGraph(engine);
    auto camera = FilterEngineTestAccessor::getCameraNode(engine);
    auto output = FilterEngineTestAccessor::getOutputNode(engine);

    assert(graph != nullptr);
    assert(camera != nullptr);
    assert(output != nullptr);

    // Verify graph contains these nodes
    auto nodes = graph->getNodes();
    assert(std::find(nodes.begin(), nodes.end(), camera) != nodes.end());
    assert(std::find(nodes.begin(), nodes.end(), output) != nodes.end());

    // Camera, Output, OES2RGB, Filter1, Filter2
    assert(nodes.size() == 5);

    // Verify topology: camera -> oes2rgb -> filter1 -> filter2 -> output
    // Based on FilterEngine.cpp:
    // newGraph->addNode(inputNode); // index 0
    // newGraph->addNode(newOutputNode); // index 1
    // buildCameraPipeline might have added OES2RGB to m_graph previously.
    // rebuildGraph adds all nodes from m_graph as FilterNodes.

    // In rebuildGraph, OES2RGB (index 2), then Filter1 (index 3), then Filter2 (index 4)
    // Wait, let's just find them by filter instance
    std::shared_ptr<FilterNode> f1Node, f2Node;
    for (const auto& n : nodes) {
        if (auto fn = std::dynamic_pointer_cast<FilterNode>(n)) {
            if (fn->getFilter() == filter1) f1Node = fn;
            if (fn->getFilter() == filter2) f2Node = fn;
        }
    }
    assert(f1Node && f1Node->getFilter() == filter1);
    assert(f2Node && f2Node->getFilter() == filter2);

    // After rebuildGraph, the nodes in the graph are NEW FilterNode instances wrapping the same filters.
    // So camera->getOutputs()[0] will not be f1Node from the PREVIOUS graph,
    // but a new FilterNode in the NEW graph.

    // Let's just verify the chain exists.
    assert(camera->getOutputs().size() == 1);
    auto next1 = std::dynamic_pointer_cast<FilterNode>(camera->getOutputs()[0]);
    assert(next1);

    assert(next1->getOutputs().size() == 1);
    auto next2 = std::dynamic_pointer_cast<FilterNode>(next1->getOutputs()[0]);
    assert(next2);

    std::cout << "test_rebuild_success passed" << std::endl;

    std::cout << "test_rebuild_success passed" << std::endl;
}

void test_rebuild_failure_rollback() {
    FilterEngine engine;
    engine.initialize();

    // 1. Setup a valid initial state
    auto filter = std::make_shared<ToggledFailureFilter>();
    engine.addFilterRaw(filter);
    assert(engine.buildCameraPipeline().isOk());

    auto oldGraph = FilterEngineTestAccessor::getGraph(engine);
    auto oldCamera = FilterEngineTestAccessor::getCameraNode(engine);
    auto oldOutput = FilterEngineTestAccessor::getOutputNode(engine);

    assert(oldGraph != nullptr);
    assert(oldCamera != nullptr);

    // 2. Trigger a rebuild that will fail
    filter->shouldFail = true;
    // Calling buildTimelinePipeline will trigger rebuildGraph
    Result res = engine.buildTimelinePipeline(nullptr, nullptr);

    assert(!res.isOk());
    assert(res.getErrorCode() == ErrorCode::ERR_GRAPH_NODE_INIT_FAILED);

    // 3. Verify Rollback: Engine state should be identical to old state
    assert(FilterEngineTestAccessor::getGraph(engine) == oldGraph);
    assert(FilterEngineTestAccessor::getCameraNode(engine) == oldCamera);
    assert(FilterEngineTestAccessor::getOutputNode(engine) == oldOutput);

    std::cout << "test_rebuild_failure_rollback passed" << std::endl;
}

void test_camera_rebuild_failure_rollback() {
    FilterEngine engine;
    engine.initialize();

    // 1. Setup a valid initial state
    auto filter = std::make_shared<ToggledFailureFilter>();
    engine.addFilterRaw(filter);
    assert(engine.buildCameraPipeline().isOk());

    auto oldGraph = FilterEngineTestAccessor::getGraph(engine);
    auto oldCamera = FilterEngineTestAccessor::getCameraNode(engine);
    auto oldOutput = FilterEngineTestAccessor::getOutputNode(engine);

    // 2. Trigger a camera rebuild that will fail
    filter->shouldFail = true;
    Result res = engine.buildCameraPipeline();

    assert(!res.isOk());

    // 3. Verify Rollback
    // If buildCameraPipeline is buggy, it might have overwritten m_cameraNode already
    assert(FilterEngineTestAccessor::getCameraNode(engine) == oldCamera);
    assert(FilterEngineTestAccessor::getGraph(engine) == oldGraph);
    assert(FilterEngineTestAccessor::getOutputNode(engine) == oldOutput);

    std::cout << "test_camera_rebuild_failure_rollback passed" << std::endl;
}

int main() {
    test_rebuild_success();
    test_rebuild_failure_rollback();
    test_camera_rebuild_failure_rollback();
    return 0;
}
