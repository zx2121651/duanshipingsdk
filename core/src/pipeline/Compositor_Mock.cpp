
#include "../../include/timeline/Compositor.h"

namespace sdk {
namespace video {
namespace timeline {

Compositor::Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine) {}

Result Compositor::renderFrameAtTime(int64_t timelineUs, FrameBufferPtr outputFb) {
    return Result::ok();
}

}
}
}
