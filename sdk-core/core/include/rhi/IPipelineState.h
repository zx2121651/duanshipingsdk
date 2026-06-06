#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include "IShaderProgram.h"
#include "IVertexArray.h"
#include "ITexture.h"

namespace sdk {
namespace video {
namespace rhi {

// Blend factor values, maps directly to GL blend factors
enum class BlendFactor {
    Zero,                // GL_ZERO
    One,                 // GL_ONE
    SrcColor,            // GL_SRC_COLOR
    OneMinusSrcColor,    // GL_ONE_MINUS_SRC_COLOR
    DstColor,            // GL_DST_COLOR
    OneMinusDstColor,    // GL_ONE_MINUS_DST_COLOR
    SrcAlpha,            // GL_SRC_ALPHA
    OneMinusSrcAlpha,    // GL_ONE_MINUS_SRC_ALPHA
    DstAlpha,            // GL_DST_ALPHA
    OneMinusDstAlpha,    // GL_ONE_MINUS_DST_ALPHA
    ConstantAlpha,       // GL_CONSTANT_ALPHA
    OneMinusConstantAlpha // GL_ONE_MINUS_CONSTANT_ALPHA
};

enum class BlendEquation {
    Add,                 // GL_FUNC_ADD
    Subtract,            // GL_FUNC_SUBTRACT
    ReverseSubtract,     // GL_FUNC_REVERSE_SUBTRACT
    Min,                 // GL_MIN
    Max                  // GL_MAX
};

enum class CompareFunc {
    Never,               // GL_NEVER
    Less,                // GL_LESS
    Equal,               // GL_EQUAL
    LessEqual,           // GL_LEQUAL
    Greater,             // GL_GREATER
    NotEqual,            // GL_NOTEQUAL
    GreaterEqual,        // GL_GEQUAL
    Always               // GL_ALWAYS
};

enum class StencilOp {
    Keep,                // GL_KEEP
    Zero,                // GL_ZERO
    Replace,             // GL_REPLACE
    Increment,           // GL_INCR
    Decrement,           // GL_DECR
    Invert,              // GL_INVERT
    IncrementWrap,       // GL_INCR_WRAP
    DecrementWrap        // GL_DECR_WRAP
};

enum class ColorWriteMask : uint32_t {
    None  = 0x0,
    Red   = 0x1,
    Green = 0x2,
    Blue  = 0x4,
    Alpha = 0x8,
    RGBA  = 0xF,
    RGB   = 0x7
};

inline uint32_t operator&(ColorWriteMask lhs, uint32_t rhs) {
    return static_cast<uint32_t>(lhs) & rhs;
}

class IShaderResourceSet {
public:
    virtual ~IShaderResourceSet() = default;

    // Bind a texture to a given texture unit slot (maps to glActiveTexture + glBindTexture)
    virtual void bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) = 0;

    // Bind a Uniform Buffer to a given binding slot
    virtual void bindUniformBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) = 0;

    // Apply all bound resources to the current GL state
    virtual void apply() = 0;
};

struct BlendDesc {
    bool blendEnabled = false;
    // Color channel blend: result = src * srcColorFactor + dst * dstColorFactor
    BlendFactor srcColorFactor = BlendFactor::SrcAlpha;
    BlendFactor dstColorFactor = BlendFactor::OneMinusSrcAlpha;
    // Alpha channel blend: result = srcA * srcAlphaFactor + dstA * dstAlphaFactor
    BlendFactor srcAlphaFactor = BlendFactor::One;
    BlendFactor dstAlphaFactor = BlendFactor::OneMinusSrcAlpha;
    BlendEquation colorBlendEquation = BlendEquation::Add;
    BlendEquation alphaBlendEquation = BlendEquation::Add;
    ColorWriteMask colorWriteMask = ColorWriteMask::RGBA;
};

struct StencilDesc {
    bool stencilTestEnabled = false;
    CompareFunc compareFunc = CompareFunc::Always;
    uint32_t referenceValue = 0;
    uint32_t compareMask = 0xFF;
    StencilOp failOp = StencilOp::Keep;
    StencilOp passOp = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
};

struct DepthStencilDesc {
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
    CompareFunc depthCompareFunc = CompareFunc::Less;
    bool stencilTestEnabled = false;       // compat shortcut; use stencilFront/back for full control
    StencilDesc stencilFront;
    StencilDesc stencilBack;
};

enum class PrimitiveTopology {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    TriangleFan
};

struct RasterizerDesc {
    bool cullFaceEnabled = false;
    // Front face winding: CW = GL_CW, CCW = GL_CCW
    bool frontFaceCCW = true;
    // Depth bias for shadow map rendering
    float depthBiasConstantFactor = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float depthBiasClamp = 0.0f;
};

struct PipelineStateDesc {
    IShaderProgram* shaderProgram = nullptr;
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleStrip;
    BlendDesc blendState;
    DepthStencilDesc depthStencilState;
    RasterizerDesc rasterizerState;
};

class [[nodiscard]] IPipelineState {
public:
    virtual ~IPipelineState() = default;
    virtual const PipelineStateDesc& getDesc() const = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
