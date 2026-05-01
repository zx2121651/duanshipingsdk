#pragma once
#include "../../include/rhi/IShaderProgram.h"

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
    ~GLShaderProgram() override;

    bool isValid() const override { return m_programId != 0; }

    void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) override;
    uint32_t getGLHandle() const { return m_programId; }

private:
    GLuint m_programId = 0;
    bool m_isCompute = false;

    GLuint loadShader(GLenum type, const char* shaderSrc);
    GLuint createProgram(const char* vertexSource, const char* fragmentSource);
};

} // namespace rhi
} // namespace video
} // namespace sdk
