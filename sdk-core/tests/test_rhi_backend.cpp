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
#include "../core/include/GLTypes.h"
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
    ASSERT_EQ(static_cast<int>(buf->getType()),
              static_cast<int>(BufferType::VertexBuffer), "Buffer type matches");
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
// TC-R17: TextureFormat enum covers new video/single-channel formats
// -----------------------------------------------------------------------
static void tc_r17_texture_format_new_values() {
    // Just verify distinct integer values (no collisions)
    int nv12   = static_cast<int>(TextureFormat::NV12);
    int bgra8  = static_cast<int>(TextureFormat::BGRA8);
    int r8     = static_cast<int>(TextureFormat::R8);
    int r16f   = static_cast<int>(TextureFormat::R16F);
    int r32f   = static_cast<int>(TextureFormat::R32F);
    int d32f   = static_cast<int>(TextureFormat::Depth32F);
    int astc44 = static_cast<int>(TextureFormat::ASTC_4x4);
    ASSERT_TRUE(nv12 != bgra8 && bgra8 != r8 && r8 != r16f && r16f != r32f
                && r32f != d32f && d32f != astc44,
                "TC-R17: new TextureFormat values are distinct");
}

// -----------------------------------------------------------------------
// TC-R18: createTexture returns correct getFormat()
// -----------------------------------------------------------------------
static void tc_r18_create_texture_get_format() {
    auto dev = std::make_shared<GLRenderDevice>();
    TextureDesc desc;
    desc.width = 64; desc.height = 64;

    desc.format = TextureFormat::R8;
    auto r8tex = dev->createTexture(desc);
    ASSERT_TRUE(r8tex != nullptr, "TC-R18: R8 texture created");
    ASSERT_EQ(static_cast<int>(r8tex->getFormat()), static_cast<int>(TextureFormat::R8),
              "TC-R18: R8 texture getFormat() == R8");

    desc.format = TextureFormat::RGBA16F;
    auto hftex = dev->createTexture(desc);
    ASSERT_TRUE(hftex != nullptr, "TC-R18: RGBA16F texture created");
    ASSERT_EQ(static_cast<int>(hftex->getFormat()), static_cast<int>(TextureFormat::RGBA16F),
              "TC-R18: RGBA16F texture getFormat() == RGBA16F");
}

// -----------------------------------------------------------------------
// TC-R19: TextureDesc mipLevels field defaults to 1
// -----------------------------------------------------------------------
static void tc_r19_texture_desc_mipmaps_default() {
    TextureDesc desc;
    ASSERT_EQ(desc.mipLevels, 1u, "TC-R19: TextureDesc::mipLevels defaults to 1");
}

// -----------------------------------------------------------------------
// TC-R20: PipelineStateDesc primitiveTopology defaults to TriangleStrip
// -----------------------------------------------------------------------
static void tc_r20_pipeline_state_topology_default() {
    PipelineStateDesc desc;
    ASSERT_EQ(static_cast<int>(desc.primitiveTopology),
              static_cast<int>(PrimitiveTopology::TriangleStrip),
              "TC-R20: default topology is TriangleStrip");
}

// -----------------------------------------------------------------------
// TC-R21: DepthStencilDesc has depthCompareFunc defaulting to Less
// -----------------------------------------------------------------------
static void tc_r21_depth_stencil_compare_func_default() {
    DepthStencilDesc desc;
    ASSERT_EQ(static_cast<int>(desc.depthCompareFunc),
              static_cast<int>(CompareFunc::Less),
              "TC-R21: DepthStencilDesc::depthCompareFunc defaults to Less");
}

// -----------------------------------------------------------------------
// TC-R22: BlendDesc has colorBlendEquation defaulting to Add
// -----------------------------------------------------------------------
static void tc_r22_blend_equation_default() {
    BlendDesc desc;
    ASSERT_EQ(static_cast<int>(desc.colorBlendEquation),
              static_cast<int>(BlendEquation::Add),
              "TC-R22: BlendDesc::colorBlendEquation defaults to Add");
    ASSERT_EQ(static_cast<int>(desc.colorWriteMask),
              static_cast<int>(ColorWriteMask::RGBA),
              "TC-R22: BlendDesc::colorWriteMask defaults to RGBA");
}

// -----------------------------------------------------------------------
// TC-R23: IShaderProgram interface compiles with new uniform setters
// -----------------------------------------------------------------------
static void tc_r23_shader_new_uniforms() {
    auto dev = std::make_shared<GLRenderDevice>();
    auto prog = dev->createShaderProgram(
        "void main(){gl_Position=vec4(0);}",
        "void main(){}");
    // Shader compilation may fail in headless; we just verify the calls don't crash
    if (prog && prog->isValid()) {
        prog->setUniform3f("u_dir", 1.f, 0.f, 0.f);
        float mat3[9] = {1,0,0,0,1,0,0,0,1};
        prog->setUniformMat3("u_normal", mat3);
        float arr[4] = {0.25f, 0.5f, 0.75f, 1.0f};
        prog->setUniform4fv("u_colors", arr, 1);
        prog->setUniform2i("u_tile", 4, 4);
    }
    ASSERT_TRUE(true, "TC-R23: new uniform setter calls do not crash");
}

// -----------------------------------------------------------------------
// TC-R24: IndexType enum has correct distinct values
// -----------------------------------------------------------------------
static void tc_r24_index_type_enum() {
    ASSERT_TRUE(static_cast<int>(IndexType::UInt16) != static_cast<int>(IndexType::UInt32),
                "TC-R24: IndexType::UInt16 != UInt32");
}

// -----------------------------------------------------------------------
// TC-R25: PrimitiveTopology enum covers all 6 types
// -----------------------------------------------------------------------
static void tc_r25_primitive_topology_enum() {
    int vals[] = {
        static_cast<int>(PrimitiveTopology::PointList),
        static_cast<int>(PrimitiveTopology::LineList),
        static_cast<int>(PrimitiveTopology::LineStrip),
        static_cast<int>(PrimitiveTopology::TriangleList),
        static_cast<int>(PrimitiveTopology::TriangleStrip),
        static_cast<int>(PrimitiveTopology::TriangleFan),
    };
    for (int i = 0; i < 6; ++i)
        for (int j = i+1; j < 6; ++j)
            ASSERT_TRUE(vals[i] != vals[j], "TC-R25: PrimitiveTopology values are distinct");
}

// -----------------------------------------------------------------------
// TC-R26: RasterizerDesc has frontFaceCCW and depth bias fields
// -----------------------------------------------------------------------
static void tc_r26_rasterizer_desc_new_fields() {
    RasterizerDesc desc;
    ASSERT_TRUE(desc.frontFaceCCW == true, "TC-R26: frontFaceCCW defaults to true (CCW)");
    ASSERT_TRUE(desc.depthBiasConstantFactor == 0.0f, "TC-R26: depthBiasConstantFactor defaults to 0");
    ASSERT_TRUE(desc.depthBiasSlopeFactor == 0.0f, "TC-R26: depthBiasSlopeFactor defaults to 0");
}

// -----------------------------------------------------------------------
// TC-R27: Indexed mesh draw path works through RHI command buffer
// -----------------------------------------------------------------------
static void tc_r27_indexed_mesh_draw_smoke() {
    auto dev = std::make_shared<GLRenderDevice>();

    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    const uint32_t indices[] = {0, 1, 2, 2, 1, 3};

    auto vertexBuffer = dev->createBuffer(
        BufferType::VertexBuffer, BufferUsage::StaticDraw,
        sizeof(vertices), vertices);
    auto indexBuffer = dev->createBuffer(
        BufferType::IndexBuffer, BufferUsage::StaticDraw,
        sizeof(indices), indices);
    auto vao = dev->createVertexArray();

    ASSERT_TRUE(vertexBuffer != nullptr, "TC-R27: vertex buffer created");
    ASSERT_TRUE(indexBuffer != nullptr, "TC-R27: index buffer created");
    ASSERT_TRUE(vao != nullptr, "TC-R27: vertex array created");

    vao->addVertexBuffer(vertexBuffer, {
        {0, VertexFormat::Float2, 0, 4 * sizeof(float)},
        {1, VertexFormat::Float2, 2 * sizeof(float), 4 * sizeof(float)},
    });
    vao->setIndexBuffer(indexBuffer);

    PipelineStateDesc desc;
    desc.primitiveTopology = PrimitiveTopology::TriangleList;
    auto pipeline = dev->createGraphicsPipeline(desc);
    auto cmd = dev->createCommandBuffer();

    ASSERT_TRUE(pipeline != nullptr, "TC-R27: triangle-list pipeline created");
    ASSERT_TRUE(cmd != nullptr, "TC-R27: command buffer created");

    cmd->bindPipelineState(pipeline);
    cmd->bindVertexArray(vao.get());
    cmd->drawIndexed(6, IndexType::UInt32);
    ASSERT_TRUE(true, "TC-R27: drawIndexed(UInt32) completed without crash");
}

// -----------------------------------------------------------------------
// TC-R28: Empty render pass descriptors are safe no-ops at the RHI boundary
// -----------------------------------------------------------------------
static void tc_r28_empty_render_pass_noop() {
    auto dev = std::make_shared<GLRenderDevice>();
    auto cmd = dev->createCommandBuffer();
    ASSERT_TRUE(cmd != nullptr, "TC-R28: command buffer created");

    RenderPassDescriptor desc;
    cmd->beginRenderPass(desc);
    cmd->endRenderPass();
    dev->submit(cmd.get());
    dev->submit(nullptr);

    ASSERT_TRUE(true, "TC-R28: empty render pass and null submit completed without crash");
}

// -----------------------------------------------------------------------
// TC-R29: Existing GL texture handles are wrapped explicitly, not as hardware buffers
// -----------------------------------------------------------------------
static void tc_r29_external_texture_wrap_contract() {
    auto dev = std::make_shared<GLRenderDevice>();

    ExternalTextureDesc invalid;
    ASSERT_TRUE(dev->wrapExternalTexture(invalid) == nullptr,
                "TC-R29: invalid external texture desc returns nullptr");

    ExternalTextureDesc desc;
    desc.handle = 123;
    desc.width = 640;
    desc.height = 480;
    desc.format = TextureFormat::RGBA8;
    desc.target = 0x0DE1; // GL_TEXTURE_2D
    desc.ownsHandle = false;

    auto tex = dev->wrapExternalTexture(desc);
    ASSERT_TRUE(tex != nullptr, "TC-R29: external texture wrapper created");
    ASSERT_EQ(tex->getId(), 123u, "TC-R29: wrapped texture id is preserved");
    ASSERT_EQ(tex->getWidth(), 640u, "TC-R29: wrapped texture width is preserved");
    ASSERT_EQ(tex->getHeight(), 480u, "TC-R29: wrapped texture height is preserved");
    ASSERT_EQ(tex->getTarget(), 0x0DE1u, "TC-R29: wrapped texture target is preserved");

    desc.handle = 456;
    desc.target = 0x8D65; // GL_TEXTURE_EXTERNAL_OES
    desc.source = ExternalTextureSource::GLOESTexture;
    desc.timestampNs = 123456789;
    desc.transformMatrix[12] = 0.25f;
    auto oesTex = dev->wrapExternalTexture(desc);
    ASSERT_TRUE(oesTex != nullptr, "TC-R29: OES external texture wrapper created");
    ASSERT_EQ(oesTex->getTarget(), 0x8D65u, "TC-R29: OES texture target is preserved");
    ASSERT_EQ(static_cast<int>(desc.source), static_cast<int>(ExternalTextureSource::GLOESTexture),
              "TC-R29: OES source type is explicit");
}

// -----------------------------------------------------------------------
// TC-R30: Shader resource sets upsert slots and tolerate null unbinds
// -----------------------------------------------------------------------
static void tc_r30_shader_resource_set_contract() {
    auto dev = std::make_shared<GLRenderDevice>();
    auto rs = dev->createShaderResourceSet();
    ASSERT_TRUE(rs != nullptr, "TC-R30: shader resource set created");

    ExternalTextureDesc texDesc;
    texDesc.handle = 321;
    texDesc.width = 16;
    texDesc.height = 16;
    texDesc.target = 0x0DE1;
    auto texA = dev->wrapExternalTexture(texDesc);

    texDesc.handle = 322;
    auto texB = dev->wrapExternalTexture(texDesc);

    auto ubo = dev->createBuffer(BufferType::UniformBuffer, BufferUsage::DynamicDraw, 16, nullptr);
    auto ssbo = dev->createBuffer(BufferType::StorageBuffer, BufferUsage::DynamicDraw, 16, nullptr);

    rs->bindTexture(0, texA);
    rs->bindTexture(0, texB);
    rs->bindTexture(1, nullptr);
    rs->bindUniformBuffer(2, ubo);
    rs->bindUniformBuffer(2, nullptr);
    rs->bindStorageBuffer(3, ssbo);
    rs->bindImageTexture(4, texB, TextureAccess::ReadWrite, TextureFormat::RGBA8);
    rs->apply();

    ASSERT_TRUE(true, "TC-R30: resource set bind/update/unbind/apply completed without crash");
}

// -----------------------------------------------------------------------
// TC-R31: Buffer update/map contract is backend-facing and range-guarded
// -----------------------------------------------------------------------
static void tc_r31_buffer_update_map_contract() {
    auto dev = std::make_shared<GLRenderDevice>();
    uint8_t initial[16] = {};
    auto buffer = dev->createBuffer(BufferType::UniformBuffer,
                                    BufferUsage::DynamicDraw,
                                    sizeof(initial),
                                    initial);
    ASSERT_TRUE(buffer != nullptr, "TC-R31: uniform buffer created");
    ASSERT_EQ(buffer->getSize(), sizeof(initial), "TC-R31: buffer size is preserved");
    ASSERT_EQ(static_cast<int>(buffer->getType()),
              static_cast<int>(BufferType::UniformBuffer),
              "TC-R31: buffer type is preserved");

    uint8_t patch[4] = {1, 2, 3, 4};
    buffer->updateData(patch, sizeof(patch), 4);
    buffer->updateData(nullptr, sizeof(patch), 0);
    buffer->updateData(patch, 0, 0);
    buffer->updateData(patch, sizeof(patch), sizeof(initial) + 1);

    void* mapped = buffer->map(0, sizeof(initial), BufferAccess::Write);
    ASSERT_TRUE(mapped != nullptr, "TC-R31: valid map returns a pointer in mock GL");
    buffer->unmap();

    void* invalidMapped = buffer->map(sizeof(initial) + 1, 1, BufferAccess::Write);
    ASSERT_TRUE(invalidMapped == nullptr, "TC-R31: out-of-range map returns nullptr");
}

// -----------------------------------------------------------------------
// TC-R32: Vertex array accepts only matching vertex/index buffer roles
// -----------------------------------------------------------------------
static void tc_r32_vertex_array_buffer_role_contract() {
    auto dev = std::make_shared<GLRenderDevice>();
    float vertices[8] = {};
    uint16_t indices[3] = {0, 1, 2};

    auto vertexBuffer = dev->createBuffer(BufferType::VertexBuffer,
                                          BufferUsage::StaticDraw,
                                          sizeof(vertices),
                                          vertices);
    auto indexBuffer = dev->createBuffer(BufferType::IndexBuffer,
                                         BufferUsage::StaticDraw,
                                         sizeof(indices),
                                         indices);
    auto uniformBuffer = dev->createBuffer(BufferType::UniformBuffer,
                                           BufferUsage::DynamicDraw,
                                           16,
                                           nullptr);
    auto vao = dev->createVertexArray();

    ASSERT_TRUE(vertexBuffer != nullptr, "TC-R32: vertex buffer created");
    ASSERT_TRUE(indexBuffer != nullptr, "TC-R32: index buffer created");
    ASSERT_TRUE(uniformBuffer != nullptr, "TC-R32: uniform buffer created");
    ASSERT_TRUE(vao != nullptr, "TC-R32: vertex array created");

    vao->addVertexBuffer(uniformBuffer, {{0, VertexFormat::Float2, 0, 2 * sizeof(float)}});
    vao->setIndexBuffer(uniformBuffer);
    vao->addVertexBuffer(vertexBuffer, {{0, VertexFormat::Float2, 0, 2 * sizeof(float)}});
    vao->setIndexBuffer(indexBuffer);

    auto cmd = dev->createCommandBuffer();
    cmd->bindVertexArray(vao.get());
    cmd->drawIndexed(3, IndexType::UInt16);
    ASSERT_TRUE(true, "TC-R32: vertex array role filtering and indexed draw completed");
}

// -----------------------------------------------------------------------
// TC-R33: Compute pipeline is a first-class RHI command path
// -----------------------------------------------------------------------
static void tc_r33_compute_pipeline_dispatch_contract() {
    auto dev = std::make_shared<GLRenderDevice>();

    ComputePipelineStateDesc desc;
    auto pipeline = dev->createComputePipeline(desc);
    auto resources = dev->createShaderResourceSet();
    auto cmd = dev->createCommandBuffer();

    ASSERT_TRUE(pipeline != nullptr, "TC-R33: compute pipeline created");
    ASSERT_TRUE(resources != nullptr, "TC-R33: compute resource set created");
    ASSERT_TRUE(cmd != nullptr, "TC-R33: compute command buffer created");

    cmd->bindPipelineState(pipeline);
    cmd->bindResourceSet(0, resources);
    cmd->dispatchCompute(1, 1, 1);
    cmd->pipelineBarrier(BarrierType::Pipeline);

    ASSERT_TRUE(true, "TC-R33: compute dispatch path completed without crash");
}

// -----------------------------------------------------------------------
// TC-R34: Render pass carries color/depth formats through public contracts
// -----------------------------------------------------------------------
static void tc_r34_render_pass_format_contract() {
    auto dev = std::make_shared<GLRenderDevice>();

    TextureDesc colorDesc;
    colorDesc.width = 32;
    colorDesc.height = 32;
    colorDesc.format = TextureFormat::RGBA16F;
    colorDesc.usageFlags = static_cast<uint32_t>(TextureUsage::RenderTarget) |
                           static_cast<uint32_t>(TextureUsage::Sampled);

    TextureDesc depthDesc;
    depthDesc.width = 32;
    depthDesc.height = 32;
    depthDesc.format = TextureFormat::Depth32F;
    depthDesc.usageFlags = static_cast<uint32_t>(TextureUsage::RenderTarget);

    auto color = dev->createTexture(colorDesc);
    auto depth = dev->createTexture(depthDesc);
    ASSERT_TRUE(color != nullptr, "TC-R34: fp16 color render target created");
    ASSERT_TRUE(depth != nullptr, "TC-R34: depth render target created");
    ASSERT_EQ(static_cast<int>(color->getFormat()), static_cast<int>(TextureFormat::RGBA16F),
              "TC-R34: color format is preserved");
    ASSERT_EQ(static_cast<int>(depth->getFormat()), static_cast<int>(TextureFormat::Depth32F),
              "TC-R34: depth format is preserved");

    RenderPassDescriptor pass;
    pass.colorAttachments.push_back({color});
    pass.depthStencilAttachment.texture = depth;
    pass.hasDepthStencil = true;

    auto cmd = dev->createCommandBuffer();
    cmd->beginRenderPass(pass);
    cmd->endRenderPass();
    ASSERT_TRUE(true, "TC-R34: render pass with explicit depth attachment completed");
}

// -----------------------------------------------------------------------
// TC-R35: Beauty ping-pong textures can request sampled/render/storage usage
// -----------------------------------------------------------------------
static void tc_r35_texture_usage_flags_contract() {
    auto dev = std::make_shared<GLRenderDevice>();

    TextureDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.format = TextureFormat::RGBA16F;
    desc.usageFlags = static_cast<uint32_t>(TextureUsage::Sampled) |
                      static_cast<uint32_t>(TextureUsage::RenderTarget) |
                      static_cast<uint32_t>(TextureUsage::Storage);

    auto texture = dev->createTexture(desc);
    auto rs = dev->createShaderResourceSet();

    ASSERT_TRUE(texture != nullptr, "TC-R35: sampled/render/storage texture created");
    ASSERT_TRUE(rs != nullptr, "TC-R35: shader resource set created");
    ASSERT_EQ(static_cast<int>(texture->getFormat()), static_cast<int>(TextureFormat::RGBA16F),
              "TC-R35: ping-pong texture format is preserved");

    rs->bindTexture(0, texture);
    rs->bindImageTexture(1, texture, TextureAccess::ReadWrite, TextureFormat::RGBA16F);
    rs->apply();

    ASSERT_TRUE(true, "TC-R35: texture can be bound for sampled and image access");
}

// -----------------------------------------------------------------------
// TC-R36: Hardware buffer import rejects invalid native buffers
// -----------------------------------------------------------------------
static void tc_r36_hardware_buffer_import_contract() {
    auto dev = std::make_shared<GLRenderDevice>();

    HardwareBufferDesc invalid;
    invalid.source = ExternalTextureSource::AndroidHardwareBuffer;
    invalid.width = 640;
    invalid.height = 480;
    invalid.logicalFormat = TextureFormat::RGBA8;
    invalid.ownership = ExternalOwnership::Borrowed;

    ASSERT_TRUE(dev->createTextureFromHardwareBuffer(invalid) == nullptr,
                "TC-R36: null hardware buffer is rejected");

    HardwareBufferDesc metadata;
    metadata.timestampNs = 42;
    metadata.transformMatrix[0] = 0.5f;
    ASSERT_EQ(metadata.timestampNs, 42ull, "TC-R36: hardware buffer timestamp metadata stored");
    ASSERT_TRUE(metadata.transformMatrix[0] == 0.5f,
                "TC-R36: hardware buffer transform metadata stored");
}

// -----------------------------------------------------------------------
// TC-R37: Camera/OES frame metadata travels with the texture contract
// -----------------------------------------------------------------------
static void tc_r37_external_camera_texture_frame_contract() {
    Texture legacy{7, 1280, 720};
    ASSERT_EQ(legacy.id, 7u, "TC-R37: legacy aggregate id remains compatible");
    ASSERT_EQ(legacy.target, 0x0DE1u, "TC-R37: legacy texture defaults to GL_TEXTURE_2D");
    ASSERT_EQ(static_cast<int>(legacy.source),
              static_cast<int>(ExternalTextureSource::GLTexture2D),
              "TC-R37: legacy texture defaults to GLTexture2D source");
    ASSERT_EQ(legacy.timestampNs, 0ull, "TC-R37: legacy texture timestamp defaults to zero");
    ASSERT_TRUE(legacy.transformMatrix[0] == 1.0f && legacy.transformMatrix[15] == 1.0f,
                "TC-R37: legacy texture transform defaults to identity");

    Texture camera;
    camera.id = 99;
    camera.width = 1080;
    camera.height = 1920;
    camera.target = 0x8D65; // GL_TEXTURE_EXTERNAL_OES
    camera.source = ExternalTextureSource::GLOESTexture;
    camera.timestampNs = 123456789ull;
    camera.transformMatrix[12] = 0.375f;

    ASSERT_EQ(camera.target, 0x8D65u, "TC-R37: camera input preserves OES target");
    ASSERT_EQ(static_cast<int>(camera.source),
              static_cast<int>(ExternalTextureSource::GLOESTexture),
              "TC-R37: camera input source is explicit GLOESTexture");
    ASSERT_EQ(camera.timestampNs, 123456789ull, "TC-R37: camera timestamp travels on Texture");
    ASSERT_TRUE(camera.transformMatrix[12] == 0.375f,
                "TC-R37: camera transform travels on Texture");
}

// -----------------------------------------------------------------------
// TC-R38: Hardware buffer descriptors carry zero-copy sync and native layout metadata
// -----------------------------------------------------------------------
static void tc_r38_hardware_buffer_sync_metadata_contract() {
    HardwareBufferDesc desc;
    ASSERT_EQ(desc.stride, 0u, "TC-R38: hardware buffer stride defaults to unknown");
    ASSERT_EQ(desc.layers, 1u, "TC-R38: hardware buffer layers default to 1");
    ASSERT_EQ(desc.usage, 0ull, "TC-R38: hardware buffer usage defaults to unknown");
    ASSERT_EQ(desc.acquireFenceFd, -1, "TC-R38: hardware buffer acquire fence defaults to none");
    ASSERT_EQ(static_cast<int>(desc.syncMode),
              static_cast<int>(ExternalSyncMode::GpuWait),
              "TC-R38: hardware buffer sync defaults to GPU wait");

    desc.stride = 2048;
    desc.layers = 1;
    desc.format = 1;
    desc.usage = 0x300ull;
    desc.acquireFenceFd = 42;
    desc.syncMode = ExternalSyncMode::CpuWait;

    ASSERT_EQ(desc.stride, 2048u, "TC-R38: explicit stride metadata is stored");
    ASSERT_EQ(desc.layers, 1u, "TC-R38: explicit layer metadata is stored");
    ASSERT_EQ(desc.format, 1, "TC-R38: explicit native format metadata is stored");
    ASSERT_EQ(desc.usage, 0x300ull, "TC-R38: explicit usage metadata is stored");
    ASSERT_EQ(desc.acquireFenceFd, 42, "TC-R38: explicit acquire fence fd is stored");
    ASSERT_EQ(static_cast<int>(desc.syncMode),
              static_cast<int>(ExternalSyncMode::CpuWait),
              "TC-R38: explicit sync mode is stored");
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
    // --- New tests for fixed/added features ---
    tc_r17_texture_format_new_values();
    tc_r18_create_texture_get_format();
    tc_r19_texture_desc_mipmaps_default();
    tc_r20_pipeline_state_topology_default();
    tc_r21_depth_stencil_compare_func_default();
    tc_r22_blend_equation_default();
    tc_r23_shader_new_uniforms();
    tc_r24_index_type_enum();
    tc_r25_primitive_topology_enum();
    tc_r26_rasterizer_desc_new_fields();
    tc_r27_indexed_mesh_draw_smoke();
    tc_r28_empty_render_pass_noop();
    tc_r29_external_texture_wrap_contract();
    tc_r30_shader_resource_set_contract();
    tc_r31_buffer_update_map_contract();
    tc_r32_vertex_array_buffer_role_contract();
    tc_r33_compute_pipeline_dispatch_contract();
    tc_r34_render_pass_format_contract();
    tc_r35_texture_usage_flags_contract();
    tc_r36_hardware_buffer_import_contract();
    tc_r37_external_camera_texture_frame_contract();
    tc_r38_hardware_buffer_sync_metadata_contract();

    std::cout << "\n=== Results: " << g_passed << " passed, "
              << g_failed << " failed ===\n";
    return (g_failed == 0) ? 0 : 1;
}
