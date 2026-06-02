#pragma once
#include "../../include/rhi/IShaderProgram.h"
#include <unordered_map>

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {
namespace rhi {

class GLShaderProgram : public IShaderProgram {
public:
    GLShaderProgram(const std::string& vertexSource, const std::string& fragmentSource);
    explicit GLShaderProgram(const std::string& computeSource);
    /// Takes ownership of a pre-linked program ID (used by multi-stage programs)
    explicit GLShaderProgram(GLuint prelinkedProgramId);
    ~GLShaderProgram() override;

    bool isValid() const override { return m_programId != 0; }

    // --- IShaderProgram interface ---
    void setUniform1i(const std::string& name, int value) override;
    void setUniform2i(const std::string& name, int x, int y) override;
    void setUniform3i(const std::string& name, int x, int y, int z) override;
    void setUniform4i(const std::string& name, int x, int y, int z, int w) override;
    void setUniform1f(const std::string& name, float value) override;
    void setUniform2f(const std::string& name, float x, float y) override;
    void setUniform3f(const std::string& name, float x, float y, float z) override;
    void setUniform4f(const std::string& name, float x, float y, float z, float w) override;
    void setUniformMat3(const std::string& name, const float* matrix3x3) override;
    void setUniformMat4(const std::string& name, const float* matrix4x4) override;
    void setUniform1fv(const std::string& name, const float* values, uint32_t count) override;
    void setUniform4fv(const std::string& name, const float* values, uint32_t count) override;
    void bind() override;
    void unbind() override;

    uint32_t getGLHandle() const { return m_programId; }
    bool isCompute() const { return m_isCompute; }

private:
    GLuint m_programId = 0;
    bool m_isCompute = false;

    // Uniform location cache: avoids repeated glGetUniformLocation calls
    mutable std::unordered_map<std::string, GLint> m_uniformCache;
    GLint getUniformLocation(const std::string& name) const;

    GLuint loadShader(GLenum type, const char* shaderSrc);
    GLuint createProgram(const char* vertexSource, const char* fragmentSource);
};

} // namespace rhi
} // namespace video
} // namespace sdk
