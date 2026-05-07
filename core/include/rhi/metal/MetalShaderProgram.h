#pragma once
#ifdef __APPLE__
#include "../IShaderProgram.h"
#include <string>

namespace sdk { namespace video { namespace rhi {

class MetalShaderProgram final : public IShaderProgram {
public:
    // library   : retained id<MTLLibrary>
    // cmdQueue  : borrowed void* to id<MTLCommandQueue> (owned by MetalRenderDevice)
    MetalShaderProgram(void* library, void* cmdQueue,
                       const std::string& vertEntry,
                       const std::string& fragEntry);
    ~MetalShaderProgram() override;

    void bind()   override;
    void unbind() override;

    void setUniformInt  (const std::string& name, int   value)  override;
    void setUniformFloat(const std::string& name, float value)  override;
    void setUniformMat4 (const std::string& name,
                         const float* m16, bool transpose)      override;

    uint64_t nativeHandle() const override {
        return reinterpret_cast<uint64_t>(m_library);
    }

private:
    void*       m_library      = nullptr; // retained id<MTLLibrary>
    void*       m_commandQueue = nullptr; // borrowed
    std::string m_vertEntry;
    std::string m_fragEntry;
};

}}} // namespace sdk::video::rhi
#endif
