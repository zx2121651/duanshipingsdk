#pragma once
#include "GLTypes.h"
#include <string>

namespace sdk {
namespace video {

/// GLES 版本梯级（对应能力 Tier）
enum class GLESVersion {
    GLES_30 = 30,  ///< Tier 0 — Fragment/Vertex, FBO, OES, YUV
    GLES_31 = 31,  ///< Tier 1 — + Compute Shader, SSBO, image2D
    GLES_32 = 32   ///< Tier 2 — + Geometry Shader, Tessellation, MSAA, ASTC core
};

/**
 * @brief 运行时 OpenGL ES 能力嗅探器 (GL Feature Sniffer)
 *
 * 核心设计思想：
 * 绝不能盲信设备的 Android 版本或 glGetString(GL_VERSION) 的标称值。
 * 很多低端 GPU 驱动存在虚标或 Bug（例如声称支持 GLES 3.1 但 Compute Shader 工作组极小导致崩溃）。
 * 因此，我们必须在 EGL Context 初始化后，逐一排查具体的 Extension 和 Capability，
 * 以此决定是开启高性能的并行计算、半精度浮点纹理 (HDR)，还是老老实实回退到最安全的 GLES 3.0。
 */
class GLContextManager {
public:
    GLContextManager() = default;
    ~GLContextManager() = default;

    /**
     * @brief 绑定到当前线程的 GL Context 后，立刻执行深度体检。
     */
    void sniffCapabilities();

    // ------------------------------------------------------------------------
    // 嗅探到的真实硬件能力标记 (Capability Flags)
    // ------------------------------------------------------------------------

    /// @brief 是否安全支持 Compute Shader (GLES 3.1+ 且工作组线程数达标)
    bool isComputeShaderSupported() const { return m_supportComputeShader; }

    /// @brief 是否支持半精度浮点纹理 (GL_OES_texture_half_float / EXT_color_buffer_half_float)
    /// 这决定了我们中间的 FBO 能不能用 GL_RGBA16F，这是实现电影级 HDR 滤镜不溢出的关键。
    bool isFP16RenderTargetSupported() const { return m_supportFP16RenderTarget; }

    /// @brief 是否支持 ASTC 终极硬件纹理压缩 (GL_KHR_texture_compression_astc_ldr)
    /// 这决定了我们加载 3D LUT 等巨大资源时，能不能将显存占用压榨到 1/4 甚至 1/10。
    bool isASTCSupported() const { return m_supportASTC; }

    /// @brief 是否支持 Vulkan
    bool isVulkanSupported() const { return m_supportVulkan; }

    /// @brief 是否支持 Metal
    bool isMetalSupported() const { return m_supportMetal; }

    // ------------------------------------------------------------------------
    // GLES 版本梯级 API（Step 1 新增）
    // ------------------------------------------------------------------------

    /// @brief 返回当前设备实际支持的 GLES 版本梯级
    GLESVersion getGLESVersion() const { return m_glesVersion; }

    /// @brief 整数版本号方便比较（30 / 31 / 32）
    int getGLESVersionInt() const { return static_cast<int>(m_glesVersion); }

    /// @brief GLES 3.2 新增：几何着色器（GL_GEOMETRY_SHADER）
    bool isGeometryShaderSupported() const { return m_supportGeoShader; }

    /// @brief GLES 3.2 新增：细分着色器（GL_TESS_CONTROL_SHADER / GL_TESS_EVALUATION_SHADER）
    bool isTessellationSupported() const { return m_supportTessShader; }

    /// @brief GLES 3.2+ 或扩展：多采样 FBO（MSAA）
    bool isMSAASupported() const { return m_supportMSAA; }

    /// @brief 设备 MSAA 最大倍率（glGetIntegerv GL_MAX_SAMPLES）
    int getMaxMSAASamples() const { return m_maxMSAASamples; }

    // ------------------------------------------------------------------------

private:
    bool checkExtension(const std::string& extensionName) const;
    bool m_sniffed = false;
    int m_majorVersion = 2;
    int m_minorVersion = 0;
    bool m_supportComputeShader = false;
    bool m_supportFP16RenderTarget = false;
    bool m_supportASTC = false;
    bool m_supportVulkan = false;
    bool m_supportMetal = false;

    // GLES 三级梯级
    GLESVersion m_glesVersion = GLESVersion::GLES_30;
    bool m_supportGeoShader  = false;
    bool m_supportTessShader = false;
    bool m_supportMSAA       = false;
    int  m_maxMSAASamples    = 1;

};

} // namespace video
} // namespace sdk
