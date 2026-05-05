#pragma once
#include <memory>
#include <vector>
#include "IShaderProgram.h"
#include "IVertexArray.h"

namespace sdk {
namespace video {
namespace rhi {

class IShaderResourceSet {
public:
    virtual ~IShaderResourceSet() = default;
};

struct BlendDesc {
    bool blendEnabled = false;
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
