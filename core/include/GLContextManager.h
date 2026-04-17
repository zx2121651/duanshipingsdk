#pragma once
#include "GLTypes.h"
#include <string>

namespace sdk {
namespace video {

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

};

} // namespace video
} // namespace sdk
