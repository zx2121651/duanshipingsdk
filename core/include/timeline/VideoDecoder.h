#pragma once
#include "../GLTypes.h"
#include <string>

namespace sdk {
namespace video {
namespace timeline {

class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;

    virtual Result open(const std::string& filePath) = 0;
    virtual Texture getFrameAt(int64_t timeUs) = 0;
    virtual void close() = 0;
};

} // namespace timeline
} // namespace video
} // namespace sdk
