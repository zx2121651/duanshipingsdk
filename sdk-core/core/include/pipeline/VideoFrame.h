#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include "../GLTypes.h"
#include "../FrameBuffer.h"

namespace sdk {
namespace video {

/**
 * Lightweight wrapper for video frame data flowing through the pipeline.
 * Holds texture ID, dimensions, presentation timestamp, and optional transform matrix.
 * Retains a shared pointer to FrameBuffer to prevent premature recycling to the pool.
 */
struct VideoFrame {
    uint32_t textureId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t timestampNs = 0;
    std::vector<float> transformMatrix;

    // Holding the buffer ensures it returns to the pool when the last node finishes with it
    FrameBufferPtr frameBuffer = nullptr;

    bool isValid() const {
        return textureId != 0 && width > 0 && height > 0;
    }
};

} // namespace video
} // namespace sdk
