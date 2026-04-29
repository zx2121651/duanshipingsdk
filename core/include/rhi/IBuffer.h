#pragma once
#include <cstdint>
#include <cstddef>

namespace sdk {
namespace video {
namespace rhi {

// Define the type of buffer
enum class BufferType {
    VertexBuffer,
    IndexBuffer,
    UniformBuffer
};

// Define the usage pattern of the buffer for driver optimization
enum class BufferUsage {
    StaticDraw,  // Modified once, drawn many times
    DynamicDraw, // Modified repeatedly, drawn many times
    StreamDraw   // Modified once, drawn at most a few times
};

// Abstract interface for any hardware buffer (VBO, IBO, UBO)
class IBuffer {
public:
    virtual ~IBuffer() = default;

    // Returns the type of this buffer
    virtual BufferType getType() const = 0;

    // Returns the total size in bytes
    virtual size_t getSize() const = 0;

    // Upload data to the buffer.
    // If offset is 0 and size == getSize(), it may reallocate/orphan the old buffer.
    virtual void updateData(const void* data, size_t size, size_t offset = 0) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
