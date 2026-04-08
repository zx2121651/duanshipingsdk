#include "../../core/include/timeline/Compositor.h"

namespace sdk {
namespace video {
namespace timeline {

Compositor::Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine) : m_timeline(timeline), m_filterEngine(engine) {}


void Compositor::initPrograms() {}
void Compositor::initCopyProgram() {}
void Compositor::copyTexture(const Texture& src, FrameBufferPtr target) {}
void Compositor::initBlendProgram() {}
void Compositor::initWipeTransitionProgram() {}
Texture Compositor::blendTextures(const Texture& bg, const Texture& fg, float opacity, FrameBufferPtr target) { return Texture(); }
Texture Compositor::transitionTextures(const Texture& bg, const Texture& fg, TransitionType type, float progress, FrameBufferPtr target) { return Texture(); }
Result Compositor::renderFrameAtTime(int64_t timelineUs, FrameBufferPtr outputFb) {
    return Result::ok();
}

}
}
}
