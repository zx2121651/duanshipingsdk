#pragma once
#include "Timeline.h"

#include "../GLTypes.h"
#include "../FrameBuffer.h"
#include <memory>
#include <vector>
#include <functional>

namespace sdk {
namespace video {
class FilterEngine;
namespace timeline {

class IDecoderPool {
public:
    virtual ~IDecoderPool() = default;
    virtual Texture getFrame(const std::string& clipId, int64_t localTimeNs) = 0;
};

class Compositor {
public:
    Compositor(std::shared_ptr<Timeline> timeline, std::shared_ptr<FilterEngine> engine);
    ~Compositor() = default;

    void setDecoderPool(std::shared_ptr<IDecoderPool> decoderPool) {
        m_decoderPool = decoderPool;
    }

    Result renderFrameAtTime(int64_t timelineNs, FrameBufferPtr outputFb);

private:
    std::shared_ptr<Timeline> m_timeline;
    std::shared_ptr<FilterEngine> m_filterEngine;
    std::shared_ptr<IDecoderPool> m_decoderPool;
    std::vector<ClipPtr> m_activeClips;

    GLuint m_blendProgram = 0;
    GLuint m_copyProgram = 0;
    GLuint m_wipeTransitionProgram = 0;

    void initPrograms();
    void initCopyProgram();
    void copyTexture(const Texture& src, FrameBufferPtr target, float opacity = 1.0f);

    void initBlendProgram();
    void initWipeTransitionProgram();

    Texture blendTextures(const Texture& bg, const Texture& fg, float opacity, FrameBufferPtr target);
    Texture transitionTextures(const Texture& bg, const Texture& fg, TransitionType type, float progress, FrameBufferPtr target);
};

} // namespace timeline
} // namespace video
} // namespace sdk
