#pragma once
#include "GLTypes.h"
#include "FrameBuffer.h"
#include <string>
#include <map>
#include <memory>
#include <any>

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {

class Filter {
public:
    Filter();
    virtual ~Filter();

    virtual void initialize();
    virtual void release();

    // Renders the input texture to an output framebuffer and returns the output texture.
    virtual Texture processFrame(const Texture& inputTexture, FrameBufferPtr outputFb);

    virtual void setParameter(const std::string& key, const std::any& value);

protected:
    // Core rendering logic to be implemented by derived classes.
    virtual void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) = 0;

    // Shader compilation helpers
    GLuint loadShader(GLenum type, const char* shaderSrc);
    GLuint createProgram(const char* vertexSource, const char* fragmentSource);

    // Virtual methods for specific shader sources
    virtual const char* getVertexShaderSource() const;
    virtual const char* getFragmentShaderSource() const = 0; // Pure virtual

    GLuint m_programId;
    std::map<std::string, std::any> m_parameters;

    // Common attributes/uniforms
    GLuint m_positionHandle;
    GLuint m_texCoordHandle;
    GLuint m_inputImageTextureHandle;
};

using FilterPtr = std::shared_ptr<Filter>;

} // namespace video
} // namespace sdk
