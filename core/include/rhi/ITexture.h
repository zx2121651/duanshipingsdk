#pragma once
#include <cstdint>

namespace sdk {
namespace video {
namespace rhi {

// RHI Texture Interface
class ITexture {
public:
    virtual ~ITexture() = default;

    // Core properties
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;

    // Retrieve underlying raw handle (e.g., GLuint) for legacy compatibility during transition
    virtual uint32_t getId() const = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
