/**
 * test_rhi_backend.cpp
 *
 * RHI 多后端单元测试（不依赖真实 GPU 上下文）：
 *   - BackendType / RenderDeviceFactory smoke tests
 *   - GLES Tier 枚举值验证
 *   - GLRenderDevice GLES 3.2 API stub 不崩溃
 *   - GLContextManager GLES 三级梯级 API
 *   - TC-R13: getCapabilities() backend 字段
 *   - TC-R14: SDK_RHI_BACKEND ENV 覆盖（GLES）
 *   - TC-R15: SDK_RHI_BACKEND ENV 覆盖（VULKAN 无 HAS_VULKAN → GLES 降级）
 *   - TC-R16: getCapabilities() 幂等 + glesVersionInt 下限
 */

#include <cassert>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <cstring>

// RHI interfaces
#include "../core/include/rhi/RenderDeviceFactory.h"
#include "../core/include/rhi/IRenderDevice.h"
#include "../core/include/rhi/IShaderProgram.h"
#include "../core/include/GLContextManager.h"
#include "../core/src/rhi/GLRenderDevice.h"

// Minimal test harness
static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << "  (" #cond ")\n"; \
            ++g_failed; \
        } else { \
            std::cout << "[PASS] " << msg << "\n"; \
            ++g_passed; \
        } \
    } while(0)

#define ASSERT_EQ(a, b, msg) ASSERT_TRUE((a) == (b), msg)

using namespace sdk::video;
using namespace sdk::video::rhi;

// -----------------------------------------------------------------------
// Test 1: BackendType enum values match JNI contract
// -----------------------------------------------------------------------
static void test_backend_type_values() {
    ASSERT_EQ(static_cast<int>(BackendType::AUTO),   0, "BackendType::AUTO == 0");
    ASSERT_EQ(static_cast<int>(BackendType::GLES),   1, "BackendType::GLES == 1");
    ASSERT_EQ(static_cast<int>(BackendType::VULKAN), 2, "BackendType::VULKAN == 2");
    ASSERT_EQ(static_cast<int>(BackendType::METAL),  3, "BackendType::METAL == 3");
}

// -----------------------------------------------------------------------
// Test 2: backendTypeName utility
// -----------------------------------------------------------------------
static void test_backend_type_name() {
    ASSERT_TRUE(std::string(backendTypeName(BackendType::AUTO))   == "AUTO",   "backendTypeName AUTO");
    ASSERT_TRUE(std::string(backendTypeName(BackendType::GLES))   == "GLES",   "backendTypeName GLES");
    ASSERT_TRUE(std::string(backendTypeName(BackendType::VULKAN)) == "VULKAN", "backendTypeName VULKAN");
    ASSERT_TRUE(std::string(backendTypeName(BackendType::METAL))  == "METAL",  "backendTypeName METAL");
}

// -----------------------------------------------------------------------
// Test 3: GLESVersion enum tier values
// -----------------------------------------------------------------------
static void test_gles_version_tier_values() {
    ASSERT_EQ(static_cast<int>(GLESVersion::GLES_30), 30, "GLESVersion::GLES_30 == 30");
    ASSERT_EQ(static_cast<int>(GLESVersion::GLES_31), 31, "GLESVersion::GLES_31 == 31");
    ASSERT_EQ(static_cast<int>(GLESVersion::GLES_32), 32, "GLESVersion::GLES_32 == 32");
}

// -----------------------------------------------------------------------
// Test 4: RenderDeviceFactory::create(GLES) returns non-null IRenderDevice
// -----------------------------------------------------------------------
static void test_factory_gles_creates_device() {
    GLContextManager ctx;
    BackendType chosen = BackendType::AUTO;
    auto dev = RenderDeviceFactory::create(BackendType::GLES, ctx, chosen);
    ASSERT_TRUE(dev != nullptr,                        "RenderDeviceFactory::create(GLES) != nullptr");
    ASSERT_EQ(static_cast<int>(chosen),
              static_cast<int>(BackendType::GLES),     "chosen backend == GLES");
}

// -----------------------------------------------------------------------
// Test 5: RenderDeviceFactory::create(AUTO) falls back to GLES on desktop
// (no Vulkan available in mock-GL environment)
// -----------------------------------------------------------------------
static void test_factory_auto_fallback_gles() {
    GLContextManager ctx;
    BackendType chosen = BackendType::AUTO;
    auto dev = RenderDeviceFactory::create(BackendType::AUTO, ctx, chosen);
    ASSERT_TRUE(dev != nullptr,                        "RenderDeviceFactory::create(AUTO) != nullptr");
    // On desktop mock-GL, must fall back to GLES (no Vulkan/Metal available)
    ASSERT_EQ(static_cast<int>(chosen),
              static_cast<int>(BackendType::GLES),     "AUTO fallback == GLES on desktop");
}

// -----------------------------------------------------------------------
// Test 6: RenderDeviceFactory::create(VULKAN) stub path — falls back to GLES
// when HAS_VULKAN is not defined
// -----------------------------------------------------------------------
static void test_factory_vulkan_stub_fallback() {
    GLContextManager ctx;
    BackendType chosen = BackendType::AUTO;
    auto dev = RenderDeviceFactory::create(BackendType::VULKAN, ctx, chosen);
    ASSERT_TRUE(dev != nullptr,                        "RenderDeviceFactory::create(VULKAN) != nullptr (fallback)");
#ifndef HAS_VULKAN
    ASSERT_EQ(static_cast<int>(chosen),
              static_cast<int>(BackendType::GLES),     "VULKAN stub fallback == GLES");
#endif
}

// -----------------------------------------------------------------------
// Test 7: RenderDeviceFactory::create(METAL) stub path — falls back to GLES
// when HAS_METAL is not defined
// -----------------------------------------------------------------------
static void test_factory_metal_stub_fallback() {
    GLContextManager ctx;
    BackendType chosen = BackendType::AUTO;
    auto dev = RenderDeviceFactory::create(BackendType::METAL, ctx, chosen);
    ASSERT_TRUE(dev != nullptr,                        "RenderDeviceFactory::create(METAL) != nullptr (fallback)");
#ifndef HAS_METAL
    ASSERT_EQ(static_cast<int>(chosen),
              static_cast<int>(BackendType::GLES),     "METAL stub fallback == GLES");
#endif
}

// -----------------------------------------------------------------------
// Test 8: GLRenderDevice createBuffer smoke test
// -----------------------------------------------------------------------
static void test_gl_render_device_create_buffer() {
    auto dev = std::make_shared<GLRenderDevice>();
    float data[4] = {1.f, 2.f, 3.f, 4.f};
    auto buf = dev->createBuffer(BufferType::VertexBuffer,
                                  BufferUsage::StaticDraw,
                                  sizeof(data), data);
    ASSERT_TRUE(buf != nullptr,                        "GLRenderDevice::createBuffer != nullptr");
    ASSERT_EQ(buf->getSize(), sizeof(data),            "Buffer size matches");
}

// -----------------------------------------------------------------------
// Test 9: GLRenderDevice createTexture smoke test
// -----------------------------------------------------------------------
static void test_gl_render_device_create_texture() {
    auto dev = std::make_shared<GLRenderDevice>();
    TextureDesc desc{};
    desc.width  = 64;
    desc.height = 64;
    desc.format = TextureFormat::RGBA8;
    desc.usageFlags = static_cast<uint32_t>(TextureUsage::RenderTarget);

    auto tex = dev->createTexture(desc);
    ASSERT_TRUE(tex != nullptr,                        "GLRenderDevice::createTexture != nullptr");
    ASSERT_EQ(tex->getWidth(),  64u,                   "Texture width == 64");
    ASSERT_EQ(tex->getHeight(), 64u,                   "Texture height == 64");
}

// -----------------------------------------------------------------------
// Test 10: GLRenderDevice createGeometryShaderProgram returns nullptr on
// non-Android (no GL_GEOMETRY_SHADER) — must not crash
// -----------------------------------------------------------------------
static void test_gl_geometry_shader_stub() {
    auto dev = std::make_shared<GLRenderDevice>();
    // Passing empty strings since mock GL compiles nothing
    auto prog = dev->createGeometryShaderProgram("", "", "");
    // On desktop mock-GL (non-Android, non-HAS_GLES32): expect nullptr (graceful fallback)
#ifndef __ANDROID__
#ifndef HAS_GLES32
    ASSERT_TRUE(prog == nullptr,                       "createGeometryShaderProgram returns nullptr on non-GLES32 desktop");
#endif
#endif
    std::cout << "[INFO] createGeometryShaderProgram completed without crash\n";
    ++g_passed;
}

// -----------------------------------------------------------------------
// Test 11: GLRenderDevice createMSAATexture smoke test (fallback on desktop)
// -----------------------------------------------------------------------
static void test_gl_msaa_texture_stub() {
    auto dev = std::make_shared<GLRenderDevice>();
    TextureDesc desc{};
    desc.width  = 128;
    desc.height = 128;
    desc.format = TextureFormat::RGBA8;
    auto tex = dev->createMSAATexture(desc, 4);
    ASSERT_TRUE(tex != nullptr,                        "createMSAATexture != nullptr (fallback on desktop)");
    std::cout << "[INFO] createMSAATexture completed without crash\n";
}

// -----------------------------------------------------------------------
// Test 12: ShaderStage enum values
// -----------------------------------------------------------------------
static void test_shader_stage_enum() {
    ASSERT_EQ(static_cast<int>(ShaderStage::Vertex),      0, "ShaderStage::Vertex == 0");
    ASSERT_EQ(static_cast<int>(ShaderStage::Fragment),     1, "ShaderStage::Fragment == 1");
    ASSERT_EQ(static_cast<int>(ShaderStage::Compute),      2, "ShaderStage::Compute == 2");
    ASSERT_EQ(static_cast<int>(ShaderStage::Geometry),     3, "ShaderStage::Geometry == 3");
    ASSERT_EQ(static_cast<int>(ShaderStage::TessControl),  4, "ShaderStage::TessControl == 4");
    ASSERT_EQ(static_cast<int>(ShaderStage::TessEval),     5, "ShaderStage::TessEval == 5");
}

// -----------------------------------------------------------------------
// TC-R13: getCapabilities() on GLRenderDevice returns GLES backend
// -----------------------------------------------------------------------
static void tc_r13_capabilities_gles_backend() {
    auto dev = std::make_shared<GLRenderDevice>();
    auto caps = dev->getCapabilities();
    ASSERT_EQ(static_cast<int>(caps.backend), static_cast<int>(BackendType::GLES),
              "TC-R13: GLRenderDevice caps.backend == GLES");
    ASSERT_TRUE(caps.rendererString != nullptr,
                "TC-R13: rendererString is non-null pointer");
}

// -----------------------------------------------------------------------
// TC-R14: ENV SDK_RHI_BACKEND=GLES forces GLES even when AUTO is requested
// -----------------------------------------------------------------------
static void tc_r14_env_override_gles() {
#ifdef _WIN32
    _putenv_s("SDK_RHI_BACKEND", "GLES");
#else
    setenv("SDK_RHI_BACKEND", "GLES", 1);
#endif
    GLContextManager ctx;
    BackendType chosen = BackendType::AUTO;
    auto dev = RenderDeviceFactory::create(BackendType::AUTO, ctx, chosen);
    ASSERT_TRUE(dev != nullptr,               "TC-R14: factory non-null with ENV=GLES");
    ASSERT_EQ(static_cast<int>(chosen), static_cast<int>(BackendType::GLES),
              "TC-R14: ENV SDK_RHI_BACKEND=GLES forces GLES on AUTO");
#ifdef _WIN32
    _putenv_s("SDK_RHI_BACKEND", "");
#else
    unsetenv("SDK_RHI_BACKEND");
#endif
}

// -----------------------------------------------------------------------
// TC-R15: ENV SDK_RHI_BACKEND=VULKAN without HAS_VULKAN falls back to GLES
// -----------------------------------------------------------------------
static void tc_r15_env_override_vulkan_fallback() {
#ifdef _WIN32
    _putenv_s("SDK_RHI_BACKEND", "VULKAN");
#else
    setenv("SDK_RHI_BACKEND", "VULKAN", 1);
#endif
    GLContextManager ctx;
    BackendType chosen = BackendType::AUTO;
    auto dev = RenderDeviceFactory::create(BackendType::AUTO, ctx, chosen);
    ASSERT_TRUE(dev != nullptr,
                "TC-R15: factory non-null even when VULKAN env set but unavailable");
#ifndef HAS_VULKAN
    ASSERT_EQ(static_cast<int>(chosen), static_cast<int>(BackendType::GLES),
              "TC-R15: VULKAN env without HAS_VULKAN falls back to GLES");
#endif
#ifdef _WIN32
    _putenv_s("SDK_RHI_BACKEND", "");
#else
    unsetenv("SDK_RHI_BACKEND");
#endif
}

// -----------------------------------------------------------------------
// TC-R16: getCapabilities() is idempotent; glesVersionInt >= 30
// -----------------------------------------------------------------------
static void tc_r16_capabilities_stable() {
    auto dev = std::make_shared<GLRenderDevice>();
    auto caps1 = dev->getCapabilities();
    auto caps2 = dev->getCapabilities();
    ASSERT_EQ(static_cast<int>(caps1.backend), static_cast<int>(caps2.backend),
              "TC-R16: getCapabilities() backend field is idempotent");
    ASSERT_TRUE(caps1.glesVersionInt >= 30,
                "TC-R16: glesVersionInt >= 30 (GLES 3.0 minimum contract)");
    ASSERT_TRUE(caps1.maxMSAASamples >= 1,
                "TC-R16: maxMSAASamples >= 1 (default)");
}

// -----------------------------------------------------------------------
int main() {
    std::cout << "========= test_rhi_backend =========\n";

    test_backend_type_values();
    test_backend_type_name();
    test_gles_version_tier_values();
    test_factory_gles_creates_device();
    test_factory_auto_fallback_gles();
    test_factory_vulkan_stub_fallback();
    test_factory_metal_stub_fallback();
    test_gl_render_device_create_buffer();
    test_gl_render_device_create_texture();
    test_gl_geometry_shader_stub();
    test_gl_msaa_texture_stub();
    test_shader_stage_enum();
    tc_r13_capabilities_gles_backend();
    tc_r14_env_override_gles();
    tc_r15_env_override_vulkan_fallback();
    tc_r16_capabilities_stable();

    std::cout << "\n=== Results: " << g_passed << " passed, "
              << g_failed << " failed ===\n";
    return (g_failed == 0) ? 0 : 1;
}
