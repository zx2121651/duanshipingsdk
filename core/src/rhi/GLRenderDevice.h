#pragma once
#include "../../include/rhi/IRenderDevice.h"
#include "../../include/rhi/ICommandBuffer.h"
#include "GLTexture.h"
#include "GLShaderProgram.h"
#include <mutex>
#include <queue>
#include <functional>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <memory>

namespace sdk {
namespace video {
namespace rhi {

// Per-device GL state shadow: avoids redundant glEnable/glDisable calls
struct ShadowState {
    bool blendEnabled = false;
    GLenum blendSrcColor = GL_ONE;
    GLenum blendDstColor = GL_ZERO;
    GLenum blendSrcAlpha = GL_ONE;
    GLenum blendDstAlpha = GL_ZERO;
    bool cullFaceEnabled = false;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
};

class GLRenderDevice; // forward

class GLPipelineState : public IPipelineState {
public:
    PipelineStateDesc desc;
    const PipelineStateDesc& getDesc() const override { return desc; }
    void apply(ShadowState& shadow);
};

class GLShaderResourceSet : public IShaderResourceSet {
public:
    void bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) override;
    void apply() override;

private:
    struct Binding { uint32_t slot; std::shared_ptr<ITexture> texture; };
    std::vector<Binding> m_bindings;
};

class GLCommandBuffer : public ICommandBuffer {
public:
    explicit GLCommandBuffer(GLRenderDevice* device) : m_device(device) {}
    ~GLCommandBuffer() override = default;

    void begin() override;
    void end() override;

    void beginRenderPass(const RenderPassDescriptor& descriptor) override;
    void endRenderPass() override;

    void setViewport(float x, float y, float width, float height) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void bindPipelineState(std::shared_ptr<IPipelineState> pso) override;
    void bindResourceSet(uint32_t setIndex, std::shared_ptr<IShaderResourceSet> resourceSet) override;
    void bindVertexArray(IVertexArray* vao) override;

    void draw(uint32_t count) override;
    void drawIndexed(uint32_t indexCount, IndexType indexType = IndexType::UInt16) override;

    void pipelineBarrier(BarrierType type) override;
    void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) override;

private:
    GLRenderDevice* m_device = nullptr;
    uint32_t m_currentFBO = 0;
    int64_t m_beginTimeNs = 0;
    PrimitiveTopology m_currentTopology = PrimitiveTopology::TriangleStrip;
};

class GLRenderDevice : public IRenderDevice {
public:
    GLRenderDevice() = default;
    ~GLRenderDevice() override;

    std::shared_ptr<ITexture> createTexture(const TextureDesc& desc) override;
    std::shared_ptr<IBuffer> createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data = nullptr) override;
    std::shared_ptr<IVertexArray> createVertexArray() override;

    std::shared_ptr<IPipelineState> createGraphicsPipeline(const PipelineStateDesc& desc) override;
    std::shared_ptr<IShaderResourceSet> createShaderResourceSet() override;

    std::shared_ptr<ICommandBuffer> createCommandBuffer() override;
    void submit(ICommandBuffer* cmdBuffer) override;

    std::shared_ptr<IShaderProgram> createShaderProgram(
        const char* vertexSrc, const char* fragmentSrc) override;

    // GLES 3.2 — 几何着色器程序（不支持时返回 nullptr）
    std::shared_ptr<IShaderProgram> createGeometryShaderProgram(
        const char* vertSrc, const char* geomSrc, const char* fragSrc) override;

    // GLES 3.2 — 细分着色器程序（不支持时返回 nullptr）
    std::shared_ptr<IShaderProgram> createTessellationProgram(
        const char* vertSrc, const char* tescSrc,
        const char* teseSrc, const char* fragSrc) override;

    // GLES 3.2 — 多采样纹理（MSAA）
    std::shared_ptr<ITexture> createMSAATexture(
        const TextureDesc& desc, int samples) override;

    std::shared_ptr<ITexture> bindExternalHardwareBuffer(void* nativeBuffer) override;

    RHICapabilities getCapabilities() const override;
    void waitIdle() override;

    // Instance-level FBO cache management (replaces former static cache)
    GLuint getOrCreateFBO(uint32_t texId, uint32_t texWidth, uint32_t texHeight);
    void removeFBOFromCache(uint32_t texId);

    void queueDeferredDeletion(std::function<void()> cleanupTask);
    void processDeferredDeletions();

    ShadowState& getShadowState() { return m_shadowState; }

private:
    std::mutex m_mutex;
    std::queue<std::function<void()>> m_deletionQueue;
    std::atomic<uint64_t> m_frameCount{0};

    // Per-device GL state (replaces file-scope statics — safe for multiple EGL contexts)
    ShadowState m_shadowState;
    std::unordered_map<uint32_t, GLuint> m_fboCache;
};

} // namespace rhi
} // namespace video
} // namespace sdk
