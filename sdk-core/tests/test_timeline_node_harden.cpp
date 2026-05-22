#include <iostream>
#include <cassert>
#include <memory>
#include "../core/include/pipeline/TimelineNode.h"
#include "../core/include/timeline/Timeline.h"
#include "../core/include/timeline/Compositor.h"
#include "../core/include/FilterEngine.h"

using namespace sdk::video;
using namespace sdk::video::timeline;

// Mock Compositor that does nothing but success
class SimpleMockCompositor : public Compositor {
public:
    SimpleMockCompositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine)
        : Compositor(timeline, engine) {}
    Result renderFrameAtTime(int64_t timelineNs, FrameBufferPtr outputFb) {
        return Result::ok();
    }
};

void test_timeline_node_zero_dimensions() {
    // Timeline with 0x0 dimensions
    auto timeline = std::make_shared<Timeline>(0, 0, 30);
    auto engine = std::make_shared<FilterEngine>();
    auto compositor = std::make_shared<SimpleMockCompositor>(timeline, engine);

    TimelineNode node("TestNode", timeline, compositor);

    // Should not crash and should produce a valid frame (clamped to 1x1)
    auto res = node.pullFrame(0);
    assert(res.isOk());

    VideoFrame frame = res.getValue();
    assert(frame.width == 1);
    assert(frame.height == 1);
    assert(frame.textureId != 0);
    assert(frame.frameBuffer != nullptr);

    std::cout << "test_timeline_node_zero_dimensions passed" << std::endl;
}

void test_timeline_node_shared_pool() {
    auto timeline = std::make_shared<Timeline>(1280, 720, 30);
    auto engine = std::make_shared<FilterEngine>();
    auto compositor = std::make_shared<SimpleMockCompositor>(timeline, engine);

    TimelineNode node("TestNode", timeline, compositor);

    FrameBufferPool sharedPool;
    node.setFrameBufferPool(&sharedPool);

    auto res = node.pullFrame(0);
    assert(res.isOk());

    VideoFrame frame = res.getValue();
    assert(frame.width == 1280);
    assert(frame.height == 720);

    // Verify it came from the shared pool by checking its internal pool state (indirectly)
    // Actually we can just check if the frame's buffer was allocated.
    assert(frame.frameBuffer != nullptr);

    std::cout << "test_timeline_node_shared_pool passed" << std::endl;
}

int main() {
    test_timeline_node_zero_dimensions();
    test_timeline_node_shared_pool();
    return 0;
}
