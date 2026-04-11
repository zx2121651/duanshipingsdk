#pragma once
#include "Filter.h"
#include "FrameBufferPool.h"
#include <string>

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {

class OES2RGBFilter : public Filter {
public:
    OES2RGBFilter();
    ~OES2RGBFilter() override = default;
    void initialize() override;
    void onProgramRecompiled() override;
protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getVertexShaderSource() const override;
    std::string getFragmentShaderSource() const override;
private:
    GLuint m_textureMatrixHandle;
    GLuint m_flipHorizontalHandle;
    GLuint m_flipVerticalHandle;
};

class BrightnessFilter : public Filter {
public:
    BrightnessFilter();
    ~BrightnessFilter() override = default;
    void initialize() override;
    void onProgramRecompiled() override;
protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
private:
    GLuint m_brightnessHandle;
};

// ----------------------------------------------------------------------------
// 高性能 Two-Pass 高斯模糊滤镜 (Gaussian Blur)
// ----------------------------------------------------------------------------
class GaussianBlurFilter : public Filter {
public:
    // 注入 FrameBufferPool 用于在 Two-Pass 之间借用临时 FBO
    GaussianBlurFilter(FrameBufferPool* pool);
    ~GaussianBlurFilter() override;
    void initialize() override;
    void onProgramRecompiled() override;

    // 重写 processFrame 因为双趟渲染不只是一次 onDraw
    Texture processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) override;

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getVertexShaderSource() const override;
    std::string getFragmentShaderSource() const override;

private:
    FrameBufferPool* m_pool;

    GLuint m_texelWidthOffsetHandle;
    GLuint m_texelHeightOffsetHandle;
    GLuint m_blurSizeHandle;
};

class LookupFilter : public Filter {
public:
    LookupFilter();
    ~LookupFilter() override;
    void initialize() override;
    void onProgramRecompiled() override;

    // Call this before processing frames to set the LUT
    void setLookupTexture(GLuint textureId);

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
private:
    GLuint m_lookupTextureHandle;
    GLuint m_intensityHandle;
    GLuint m_lookupTextureId;
};

class BilateralFilter : public Filter {
public:
    BilateralFilter();
    ~BilateralFilter() override = default;
    void initialize() override;
    void onProgramRecompiled() override;
protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
private:
    GLuint m_texelWidthOffsetHandle;
    GLuint m_texelHeightOffsetHandle;
    GLuint m_distanceNormalizationFactorHandle;
};

class CinematicLookupFilter : public Filter {
public:
    CinematicLookupFilter();
    ~CinematicLookupFilter() override;
    void initialize() override;
    void onProgramRecompiled() override;

    // Set the 512x512 2D LUT texture
    void setLookupTexture(GLuint textureId);

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
private:
    GLuint m_lookupTextureHandle;
    GLuint m_intensityHandle;
    GLuint m_lookupTextureId;
};

} // namespace video
} // namespace sdk

// ----------------------------------------------------------------------------
// Compute Shader 滤镜 (仅 Android GLES 3.1)
// ----------------------------------------------------------------------------
#ifdef __ANDROID__
namespace sdk {
namespace video {

class ComputeBlurFilter : public Filter {
public:
    ComputeBlurFilter();
    ~ComputeBlurFilter() override;
    void initialize() override;

    // Compute Shader 不需要 Vertex 和 Fragment Shader
    Texture processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) override;

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getVertexShaderSource() const override { return ""; }
    std::string getFragmentShaderSource() const override { return ""; }

    const char* getComputeShaderSource() const;

private:
    GLuint m_computeProgramId;
    GLuint m_blurSizeHandle;
};
#endif


} // namespace video
} // namespace sdk
