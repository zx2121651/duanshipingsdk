#pragma once
#include <memory>
#include <vector>
#include "IBuffer.h"

namespace sdk {
namespace video {
namespace rhi {

// Data types for vertex attributes
enum class VertexFormat {
    Float1,
    Float2,
    Float3,
    Float4,
    Byte4,
    UByte4_Normalized // Typically used for colors
};

// Describes a single attribute in a vertex buffer
struct VertexAttribute {
    uint32_t location;     // The layout(location = X) in the shader
    VertexFormat format;   // Data type
    uint32_t offset;       // Offset in bytes from the start of the vertex
    uint32_t stride;       // Stride in bytes to the next element
};

// Abstract interface for Vertex Array Object (VAO)
class IVertexArray {
public:
    virtual ~IVertexArray() = default;

    // Bind a vertex buffer with its corresponding layout
    // In GLES, this usually binds the VBO and sets up glVertexAttribPointer
    virtual void addVertexBuffer(std::shared_ptr<IBuffer> vertexBuffer, const std::vector<VertexAttribute>& attributes) = 0;

    // Optional: Bind an index buffer for indexed drawing
    virtual void setIndexBuffer(std::shared_ptr<IBuffer> indexBuffer) = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
