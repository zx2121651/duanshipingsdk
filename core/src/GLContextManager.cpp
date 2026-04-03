#include "../include/GLContextManager.h"
#include <iostream>
#include <sstream>

#ifdef __ANDROID__
    #include <android/log.h>
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GLContextManager", __VA_ARGS__)
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GLContextManager", __VA_ARGS__)
#else
    #define LOGE(...) std::cerr << "GLContextManager: " << __VA_ARGS__ << std::endl
    #define LOGI(...) std::cout << "GLContextManager: " << __VA_ARGS__ << std::endl
#endif

namespace sdk {
namespace video {

void GLContextManager::sniffCapabilities() {
    if (m_sniffed) return;

    // 1. 获取标称的主次版本号 (例如 "OpenGL ES 3.1 v1.r22...")
    const char* versionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (versionStr) {
        LOGI("GL_VERSION detected: %s", versionStr);
        // 粗略解析 "OpenGL ES X.Y"
        std::string vStr(versionStr);
        size_t pos = vStr.find("OpenGL ES ");
        if (pos != std::string::npos) {
            std::stringstream ss(vStr.substr(pos + 10));
            char dot;
            ss >> m_majorVersion >> dot >> m_minorVersion;
        }
    }

    // 2. 深度体检：不要迷信标称版本，逐一排查 Extensions 和硬件上限
    LOGI("Hardware Capabilities Sniffing Started...");

    // ========================================================================
    // [检查点 A]：半精度浮点纹理 (HDR FBO 的基础)
    // 很多 GLES 3.0 设备不支持将 FBO 绑定为 GL_RGBA16F 格式，这会导致曝光死白。
    // 我们必须确保设备明确支持 OES_texture_half_float 或 EXT_color_buffer_half_float 扩展。
    // ========================================================================
    if (checkExtension("GL_OES_texture_half_float") || checkExtension("GL_EXT_color_buffer_half_float")) {
        m_supportFP16RenderTarget = true;
        LOGI("=> Feature FP16 Render Target (HDR FBO): SAFE TO USE");
    } else {
        m_supportFP16RenderTarget = false;
        LOGI("=> Feature FP16 Render Target (HDR FBO): FALLBACK TO RGB565 / RGBA8");
    }

    // ========================================================================
    // [检查点 B]：ASTC 硬件纹理压缩
    // ASTC (Adaptive Scalable Texture Compression) 是 GLES 3.2 强制要求的标准。
    // 它可以将贴图或 LUT 的显存占用压缩到 1/4 甚至 1/10，并且不牺牲肉眼画质。
    // ========================================================================
    if (checkExtension("GL_KHR_texture_compression_astc_ldr")) {
        m_supportASTC = true;
        LOGI("=> Feature ASTC Hardware Compression: SAFE TO USE");
    } else {
        m_supportASTC = false;
        LOGI("=> Feature ASTC Hardware Compression: FALLBACK TO UNCOMPRESSED PNG/JPEG");
    }

    // ========================================================================
    // [检查点 C]：Compute Shader 算力底线验证 (这是防崩溃的重中之重)
    // 很多廉价芯片的 GLES 3.1 驱动写得很烂，声称支持计算着色器，但它能分配的并发线程极小。
    // 如果我们不探测而直接用 local_size_x=16 (需要 256 个 invocations) 去 glDispatchCompute，
    // 廉价芯片的驱动内核会直接 Panic 导致 App 闪退。
    // ========================================================================
#ifdef __ANDROID__
    if (m_majorVersion >= 3 && m_minorVersion >= 1) {
        // iOS 和旧版本 GLES 头文件没有以下宏，所以必须套一层宏墙。
        // 读取 GPU 支持的最大计算工作组调用数量
        GLint maxInvocations = 0;
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxInvocations);

        LOGI("Max Compute Work Group Invocations: %d", maxInvocations);

        // 我们的 ComputeBlurFilter Shader 要求至少有 16x16=256 的并发线程数。
        if (maxInvocations >= 256) {
            m_supportComputeShader = true;
            LOGI("=> Feature Compute Shader Parallelism: SAFE TO USE (Capacity > 256)");
        } else {
            m_supportComputeShader = false;
            LOGE("=> Feature Compute Shader Parallelism: REJECTED! Hardware capacity too low (%d < 256), driver will crash. Fallback to Fragment Shader Two-pass.", maxInvocations);
        }
    } else {
        m_supportComputeShader = false;
        LOGI("=> Feature Compute Shader Parallelism: REJECTED! Required GLES 3.1+");
    }
#else
    // 苹果设备即使在最新的 iPhone 15 Pro 上，原生的 OpenGL ES 也永远只支持到 3.0，
    // 绝对不能强行调用 glDispatchCompute，否则必然闪退，必须在此拦截。
    m_supportComputeShader = false;
    LOGI("=> Feature Compute Shader Parallelism: REJECTED! iOS Apple Silicon unsupported. Use Metal instead.");
#endif

    LOGI("Hardware Capabilities Sniffing Completed.");
    m_sniffed = true;
}

bool GLContextManager::checkExtension(const std::string& extensionName) const {
    if (m_majorVersion >= 3) {
        GLint numExtensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
        for (GLint i = 0; i < numExtensions; ++i) {
            const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
            if (ext && extensionName == ext) {
                return true;
            }
        }
    } else {
        const char* exts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (exts) {
            std::string extsStr(exts);
            if (extsStr.find(extensionName) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

} // namespace video
} // namespace sdk
