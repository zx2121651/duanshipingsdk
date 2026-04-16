#include "../../core/include/timeline/Compositor.h"

namespace sdk {
namespace video {
namespace timeline {

Compositor::Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine) : m_timeline(timeline), m_filterEngine(engine) {}


Result Compositor::initPrograms() { return Result::ok(); }
Result Compositor::initCopyProgram() { return Result::ok(); }
Result Compositor::copyTexture(const Texture& src, FrameBufferPtr target, float opacity) { return Result::ok(); }
Result Compositor::initBlendProgram() { return Result::ok(); }
Result Compositor::initWipeTransitionProgram() { return Result::ok(); }
ResultPayload<Texture> Compositor::blendTextures(const Texture& bg, const Texture& fg, float opacity, FrameBufferPtr target) { return ResultPayload<Texture>::ok(Texture()); }
ResultPayload<Texture> Compositor::transitionTextures(const Texture& bg, const Texture& fg, TransitionType type, float progress, FrameBufferPtr target) { return ResultPayload<Texture>::ok(Texture()); }
Result Compositor::renderFrameAtTime(int64_t timelineNs, FrameBufferPtr outputFb) {
    return Result::ok();
}

}
}
}
