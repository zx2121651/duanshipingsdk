#pragma once
#ifdef HAS_METAL

#include "../../../include/rhi/IShaderProgram.h"
#include <string>
#include <unordered_map>
#include <cstdint>
#include <vector>

#ifdef __OBJC__
#   import <Metal/Metal.h>
    using MTLDeviceRef   = id<MTLDevice>;
    using MTLFunctionRef = id<MTLFunction>;
    using MTLLibraryRef  = id<MTLLibrary>;
#else
    using MTLDeviceRef   = void*;
    using MTLFunctionRef = void*;
    using MTLLibraryRef  = void*;
#endif

namespace sdk {
namespace video {
namespace rhi {

class MetalShaderProgram : public IShaderProgram {
public:
    static std::shared_ptr<MetalShaderProgram> createFromMSL(
        MTLDeviceRef device, const char* vertMSL, const char* fragMSL);

    static std::shared_ptr<MetalShaderProgram> createTessellationFromMSL(
        MTLDeviceRef device, const char* kernelMSL, const char* fragMSL);

    ~MetalShaderProgram() override = default;

    bool isValid() const override { return m_vertFunction != nullptr; }

    void setUniform1i(const std::string& name, int value)   override;
    void setUniform1f(const std::string& name, float value) override;
    void setUniform2f(const std::string& name, float x, float y)            override;
    void setUniform4f(const std::string& name, float x, float y, float z, float w) override;
    void setUniformMat4(const std::string& name, const float* matrix4x4)    override;
    void bind()   override {}
    void unbind() override {}

    MTLFunctionRef vertFunction() const { return m_vertFunction; }
    MTLFunctionRef fragFunction() const { return m_fragFunction; }

    // Uniform buffer (mapped to Metal buffer at index 0)
    const uint8_t* uniformData() const { return m_uniformData.data(); }
    size_t         uniformSize() const { return m_uniformData.size(); }

private:
    MetalShaderProgram() = default;
    MTLFunctionRef m_vertFunction = nullptr;
    MTLFunctionRef m_fragFunction = nullptr;

    std::vector<uint8_t>                      m_uniformData;
    std::unordered_map<std::string, uint32_t> m_uniformOffsets;
    uint32_t m_uniformCursor = 0;

    void registerUniform(const std::string& name, uint32_t size);
    template<typename T>
    void writeUniform(const std::string& name, const T& val);
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_METAL
