#pragma once
#include <memory>
#include <vector>
#include "IShaderProgram.h"
#include "IVertexArray.h" // For VertexAttribute, though ideally vertex layout should be decoupled from buffer bindings

namespace sdk {
namespace video {
namespace rhi {

// Resource Binding (Descriptor Set)
class IShaderResourceSet {
public:
    virtual ~IShaderResourceSet() = default;
};

// Blend State
struct BlendState {
    bool blendEnabled = false;
    // Simplification for the skeleton
};

// Rasterization State
struct RasterizationState {
    bool cullFaceEnabled = false;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
};

// Vertex Layout Descriptor
struct VertexLayoutDescriptor {
    std::vector<VertexAttribute> attributes;
};

struct GraphicsPipelineDescriptor {
    std::shared_ptr<IShaderProgram> shaderProgram;
    VertexLayoutDescriptor vertexLayout;
    BlendState blendState;
    RasterizationState rasterizationState;
};

class IPipelineState {
public:
    virtual ~IPipelineState() = default;
};

} // namespace rhi
} // namespace video
} // namespace sdk
