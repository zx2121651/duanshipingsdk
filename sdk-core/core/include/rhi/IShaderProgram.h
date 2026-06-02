#pragma once
#include <string>
#include <cstdint>

namespace sdk {
namespace video {
namespace rhi {

/// GLSL/SPIR-V/MSL 着色器阶段枚举（跨后端统一表示）
enum class ShaderStage {
    Vertex,       ///< #version 300+ es  / vert
    Fragment,     ///< #version 300+ es  / frag
    Compute,      ///< #version 310+ es  / comp
    Geometry,     ///< #version 320 es   / GL_GEOMETRY_SHADER
    TessControl,  ///< #version 320 es   / GL_TESS_CONTROL_SHADER
    TessEval      ///< #version 320 es   / GL_TESS_EVALUATION_SHADER
};

// Abstract interface for a compiled and linked shader program on the GPU
class IShaderProgram {
public:
    virtual ~IShaderProgram() = default;

    // Returns true if the program compiled and linked successfully
    virtual bool isValid() const = 0;

    // --- Uniform Setters ---
    // Location is resolved by name; results are cached internally.
    virtual void setUniform1i(const std::string& name, int value) = 0;
    virtual void setUniform2i(const std::string& name, int x, int y) = 0;
    virtual void setUniform3i(const std::string& name, int x, int y, int z) = 0;
    virtual void setUniform4i(const std::string& name, int x, int y, int z, int w) = 0;
    virtual void setUniform1f(const std::string& name, float value) = 0;
    virtual void setUniform2f(const std::string& name, float x, float y) = 0;
    virtual void setUniform3f(const std::string& name, float x, float y, float z) = 0;
    virtual void setUniform4f(const std::string& name, float x, float y, float z, float w) = 0;
    virtual void setUniformMat3(const std::string& name, const float* matrix3x3) = 0;
    virtual void setUniformMat4(const std::string& name, const float* matrix4x4) = 0;
    // Array setters
    virtual void setUniform1fv(const std::string& name, const float* values, uint32_t count) = 0;
    virtual void setUniform4fv(const std::string& name, const float* values, uint32_t count) = 0;

    // Activate this program for subsequent draw/dispatch calls
    virtual void bind() = 0;
    virtual void unbind() = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk
