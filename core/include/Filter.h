#pragma once
#include "GLTypes.h"
#include "ShaderManager.h"
#include "FrameBuffer.h"
#include "rhi/IRenderDevice.h"
#include "rhi/IVertexArray.h"
#include <string>
#include <map>
#include <memory>
#include <any>


namespace sdk {
namespace video {

class Filter {
public:
    Filter();
    virtual ~Filter();

    virtual Result initialize();
    virtual void onProgramRecompiled() {}
    virtual Result recompileProgram();
    void setShaderManager(std::shared_ptr<ShaderManager> manager) { m_shaderManager = manager; }
    void setRenderDevice(std::shared_ptr<rhi::IRenderDevice> device) { m_renderDevice = device; }
    void setQuadVao(std::shared_ptr<rhi::IVertexArray> vao) { m_quadVao = vao; }
    virtual void release();

    // Renders the input texture to an output framebuffer and returns the output texture.
    virtual ResultPayload<Texture> processFrame(const Texture& inputTexture, FrameBufferPtr outputFb);

    virtual void setParameter(const std::string& key, const std::any& value);
    virtual void setParameterMat4(const std::string& key, const float* matrix);

protected:
    std::shared_ptr<ShaderManager> m_shaderManager;
    std::shared_ptr<rhi::IRenderDevice> m_renderDevice;
    std::shared_ptr<rhi::IVertexArray> m_quadVao;
    // Core rendering logic to be implemented by derived classes.
    virtual void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) = 0;

    // Shader compilation helpers
    uint32_t loadShader(uint32_t type, const char* shaderSrc);
    uint32_t createProgram(const char* vertexSource, const char* fragmentSource);

    // Virtual methods for specific shader sources
    virtual std::string getVertexShaderSource() const;
    virtual std::string getFragmentShaderSource() const = 0; // Pure virtual
    virtual std::string getVertexShaderName() const;
    virtual std::string getFragmentShaderName() const = 0;

    uint32_t m_programId;
    std::map<std::string, std::any> m_parameters;
    std::map<std::string, std::array<float, 16>> m_mat4Parameters;

    // Common attributes/uniforms
    uint32_t m_inputImageTextureHandle;
};

using FilterPtr = std::shared_ptr<Filter>;

} // namespace video
} // namespace sdk
