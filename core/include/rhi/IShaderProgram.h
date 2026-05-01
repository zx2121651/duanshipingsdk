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



    // Dispatch compute shader execution
    virtual void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
