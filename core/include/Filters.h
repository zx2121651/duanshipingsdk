#pragma once
#include "Filter.h"
#include "FrameBufferPool.h"
#include "rhi/IRenderDevice.h"
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
    std::string getVertexShaderName() const override { return "oes_to_rgb.vert"; }
    std::string getFragmentShaderName() const override { return "oes_to_rgb.frag"; }
    OES2RGBFilter();
    ~OES2RGBFilter() override = default;
    Result initialize() override;
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
    std::string getVertexShaderName() const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "brightness.frag"; }
    BrightnessFilter();
    ~BrightnessFilter() override = default;
    Result initialize() override;
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
    std::string getVertexShaderName() const override { return "gaussian_blur.vert"; }
    std::string getFragmentShaderName() const override { return "gaussian_blur.frag"; }
    // 注入 FrameBufferPool 用于在 Two-Pass 之间借用临时 FBO
    GaussianBlurFilter(FrameBufferPool* pool);
    ~GaussianBlurFilter() override;
    Result initialize() override;
    void onProgramRecompiled() override;

    // 重写 processFrame 因为双趟渲染不只是一次 onDraw
    ResultPayload<Texture> processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) override;

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
    std::string getVertexShaderName() const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "lookup.frag"; }
    LookupFilter();
    ~LookupFilter() override;
    Result initialize() override;
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
    std::string getVertexShaderName() const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "bilateral.frag"; }
    BilateralFilter();
    ~BilateralFilter() override = default;
    Result initialize() override;
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
    std::string getVertexShaderName() const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "cinematic_lookup.frag"; }
    CinematicLookupFilter();
    ~CinematicLookupFilter() override;
    Result initialize() override;
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

// ----------------------------------------------------------------------------
// 3D LUT colour grading filter (GLES 3.0 GL_TEXTURE_3D, 64x64x64)
// ----------------------------------------------------------------------------
class LUT3DFilter : public Filter {
public:
    std::string getVertexShaderName() const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "lut3d.frag"; }
    LUT3DFilter();
    ~LUT3DFilter() override;
    Result initialize() override;
    void onProgramRecompiled() override;
    void setLUT(const uint8_t* rgbData, int size);
    void setIntensity(float intensity);
protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;
private:
    GLuint m_lut3dTexture   = 0;
    GLuint m_lut3dHandle    = 0;
    GLuint m_intensityHandle = 0;
    float  m_intensity      = 1.0f;
    static constexpr int LUT_SIZE = 64;
};

// ----------------------------------------------------------------------------
// 3D LUT colour grading filter (GLES 3.0 GL_TEXTURE_3D, 64x64x64)
// See LUT3DFilter.h for the full declaration (included via FilterFactory.h)
// ----------------------------------------------------------------------------

class NightVisionFilter : public Filter {
public:
    std::string getVertexShaderName() const override { return "default.vert"; }
    std::string getFragmentShaderName() const override { return "night_vision.frag"; }
    NightVisionFilter() = default;
    ~NightVisionFilter() override = default;
    Result initialize() override;
    void onProgramRecompiled() override;

    // Phase 1 RHI Transition method
    void setRenderDevice(std::shared_ptr<rhi::IRenderDevice> device) { m_device = device; }

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getFragmentShaderSource() const override;

private:
    std::shared_ptr<rhi::IRenderDevice> m_device;
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
    std::string getVertexShaderName()   const override { return ""; }
    std::string getFragmentShaderName() const override { return ""; }
    // Two-pass separable Gaussian shaders (H then V)
    std::string getComputeShaderNameH() const { return "compute_blur_h.comp"; }
    std::string getComputeShaderNameV() const { return "compute_blur_v.comp"; }

    ComputeBlurFilter();
    ~ComputeBlurFilter() override;
    Result initialize() override;

    ResultPayload<Texture> processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) override;

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;
    std::string getVertexShaderSource()   const override { return ""; }
    std::string getFragmentShaderSource() const override { return ""; }

private:
    // Separable Gaussian: two compute programs (H and V pass)
    GLuint m_computeProgramH  = 0;
    GLuint m_computeProgramV  = 0;

    // Per-program uniform locations
    GLint  m_radiusHandleH    = -1;
    GLint  m_weightsHandleH   = -1;
    GLint  m_radiusHandleV    = -1;
    GLint  m_weightsHandleV   = -1;

    // Intermediate texture for H-pass output
    GLuint m_tempTexId        = 0;
    int    m_tempTexWidth     = 0;
    int    m_tempTexHeight    = 0;

    // Cached Gaussian weights (normalised, for radius 0..MAX_RADIUS)
    static constexpr int MAX_RADIUS = 16;
    float  m_weights[MAX_RADIUS + 1] = {};
    int    m_lastRadius               = -1;   // triggers recompute when radius changes

    // Helpers
    Result compileComputeProgram(const std::string& shaderName, GLuint& outProgId);
    void   recomputeWeights(int radius);
    void   ensureTempTexture(int width, int height);
};

} // namespace video
} // namespace sdk
#endif
