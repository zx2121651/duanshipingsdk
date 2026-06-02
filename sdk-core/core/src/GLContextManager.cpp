#include "../include/GLContextManager.h"
#include <iostream>
#include <sstream>

#ifdef __ANDROID__
    #include <android/log.h>
    #include <dlfcn.h>
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GLContextManager", __VA_ARGS__)
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GLContextManager", __VA_ARGS__)
#else
    #define LOGE(...) std::cerr << "GLContextManager: " << __VA_ARGS__ << std::endl

#include <cstdarg>
#include <cstdio>
void logi(const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    std::cout << "GLContextManager: " << buffer << std::endl;
}
#define LOGI logi

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
    // [检查点 C]：Vulkan 和 Metal 后端探测
    // ========================================================================
#ifdef __ANDROID__
    // 真实探测：尝试加载 libvulkan.so 动态库
    // 低端机 / x86 模拟器可能缺少此库，不能盲目标记支持
    {
        void* vulkanLib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (vulkanLib) {
            m_supportVulkan = true;
            dlclose(vulkanLib);
            LOGI("=> Feature Vulkan Backend: SUPPORTED (libvulkan.so found)");
        } else {
            m_supportVulkan = false;
            LOGI("=> Feature Vulkan Backend: NOT SUPPORTED (libvulkan.so not found)");
        }
    }
    m_supportMetal = false;
#elif defined(__APPLE__)
    m_supportVulkan = false;
    m_supportMetal = true; // iOS always supports Metal on Apple Silicon
    LOGI("=> Feature Metal Backend: SUPPORTED (Stub)");
#else
    m_supportVulkan = false;
    m_supportMetal = false;
#endif

    // ========================================================================
    // [检查点 D]：Compute Shader 算力底线验证 (这是防崩溃的重中之重)
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

    // ========================================================================
    // [检查点 E]：GLES 版本梯级 + GLES 3.2 专属能力
    // ========================================================================

    // E-1. 确定 GLESVersion 枚举值
    if (m_majorVersion == 3 && m_minorVersion >= 2)
        m_glesVersion = GLESVersion::GLES_32;
    else if (m_majorVersion == 3 && m_minorVersion >= 1)
        m_glesVersion = GLESVersion::GLES_31;
    else
        m_glesVersion = GLESVersion::GLES_30;

    LOGI("=> GLES Version Tier: %d", static_cast<int>(m_glesVersion));

    // E-2. 几何着色器 (GLES 3.2 core; 3.1 可通过 OES 扩展获得)
#ifdef __ANDROID__
    if (m_glesVersion == GLESVersion::GLES_32) {
        m_supportGeoShader = true;
        LOGI("=> Feature Geometry Shader: SUPPORTED (GLES 3.2 core)");
    } else if (checkExtension("GL_EXT_geometry_shader") ||
               checkExtension("GL_OES_geometry_shader")) {
        m_supportGeoShader = true;
        LOGI("=> Feature Geometry Shader: SUPPORTED (OES/EXT extension)");
    } else {
        m_supportGeoShader = false;
        LOGI("=> Feature Geometry Shader: NOT SUPPORTED");
    }

    // E-3. 细分着色器 (GLES 3.2 core; 3.1 可通过 OES 扩展获得)
    if (m_glesVersion == GLESVersion::GLES_32) {
        m_supportTessShader = true;
        LOGI("=> Feature Tessellation Shader: SUPPORTED (GLES 3.2 core)");
    } else if (checkExtension("GL_EXT_tessellation_shader") ||
               checkExtension("GL_OES_tessellation_shader")) {
        m_supportTessShader = true;
        LOGI("=> Feature Tessellation Shader: SUPPORTED (OES/EXT extension)");
    } else {
        m_supportTessShader = false;
        LOGI("=> Feature Tessellation Shader: NOT SUPPORTED");
    }

    // E-4. MSAA FBO (GLES 3.0+ 支持 glRenderbufferStorageMultisample，
    //               GLES 3.2 强制支持 GL_TEXTURE_2D_MULTISAMPLE)
    {
        glGetIntegerv(GL_MAX_SAMPLES, &m_maxMSAASamples);
        m_supportMSAA = (m_maxMSAASamples >= 4);
        LOGI("=> Feature MSAA: %s (maxSamples=%d)",
             m_supportMSAA ? "SUPPORTED" : "LIMITED", m_maxMSAASamples);
    }

    // E-5. ASTC：GLES 3.2 强制包含，低版本通过扩展
    // 注：原检查点 B 只看扩展，此处修正为统一入口
    if (m_glesVersion == GLESVersion::GLES_32) {
        m_supportASTC = true; // GLES 3.2 mandatory
        LOGI("=> Feature ASTC: SUPPORTED (GLES 3.2 core mandatory)");
    }
    // 低版本路径已在检查点 B 设置，此处不重复
#else
    // iOS/桌面：GLES 3.2 不可用，维持现有检测结果
    m_glesVersion    = GLESVersion::GLES_30;
    m_supportGeoShader  = false;
    m_supportTessShader = false;
    m_supportMSAA       = false;
    m_maxMSAASamples    = 1;
    LOGI("=> GLES Tier 2 features: REJECTED (non-Android platform, use Metal instead)");
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
