#pragma once
#include "Filter.h"
#include <string>

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {

class OESInputFilter : public Filter {
public:
    OESInputFilter();
    void initialize() override;
protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    const char* getFragmentShaderSource() const override;
};

class BrightnessFilter : public Filter {
public:
    BrightnessFilter();
    void initialize() override;
protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    const char* getFragmentShaderSource() const override;
private:
    GLuint m_brightnessHandle;
};

class GaussianBlurFilter : public Filter {
public:
    GaussianBlurFilter();
    void initialize() override;
protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    const char* getFragmentShaderSource() const override;
private:
    GLuint m_texelWidthOffsetHandle;
    GLuint m_texelHeightOffsetHandle;
    GLuint m_blurSizeHandle;
};

class LookupFilter : public Filter {
public:
    LookupFilter();
    ~LookupFilter() override;
    void initialize() override;

    // Call this before processing frames to set the LUT
    void setLookupTexture(GLuint textureId);

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    const char* getFragmentShaderSource() const override;
private:
    GLuint m_lookupTextureHandle;
    GLuint m_intensityHandle;
    GLuint m_lookupTextureId;
};

} // namespace video
} // namespace sdk
