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

class IShaderResourceSet {
public:
    virtual ~IShaderResourceSet() = default;

    // Bind a texture to a given texture unit slot (maps to glActiveTexture + glBindTexture)
    virtual void bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) = 0;

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
};

struct DepthStencilDesc {
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
};

struct RasterizerDesc {
    bool cullFaceEnabled = false;
};

struct PipelineStateDesc {
    IShaderProgram* shaderProgram = nullptr;
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
