#pragma once
#include <string>
#include <cstdint>

namespace sdk {
namespace video {
namespace rhi {

// Abstract interface for a compiled and linked shader program on the GPU
class IShaderProgram {
public:
    virtual ~IShaderProgram() = default;

    // Returns true if the program compiled and linked successfully
    virtual bool isValid() const = 0;

    // Get the low-level handle for legacy functions during transition
    virtual uint32_t getGLHandle() const = 0;

    // Connect a uniform block in the shader to a specific binding point for UBOs
    virtual void bindUniformBlock(const std::string& blockName, uint32_t bindingPoint) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
