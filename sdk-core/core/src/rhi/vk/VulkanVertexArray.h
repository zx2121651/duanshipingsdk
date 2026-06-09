#pragma once
#ifdef HAS_VULKAN

#include "../../../include/rhi/IVertexArray.h"

namespace sdk {
namespace video {
namespace rhi {

class VulkanVertexArray : public IVertexArray {
public:
    void addVertexBuffer(std::shared_ptr<IBuffer> vertexBuffer,
                         const std::vector<VertexAttribute>& attributes) override;
    void setIndexBuffer(std::shared_ptr<IBuffer> indexBuffer) override;

    const std::vector<std::shared_ptr<IBuffer>>& vertexBuffers() const { return m_vertexBuffers; }
    const std::vector<std::vector<VertexAttribute>>& vertexAttributes() const { return m_vertexAttributes; }
    std::shared_ptr<IBuffer> indexBuffer() const { return m_indexBuffer; }

private:
    std::vector<std::shared_ptr<IBuffer>> m_vertexBuffers;
    std::vector<std::vector<VertexAttribute>> m_vertexAttributes;
    std::shared_ptr<IBuffer> m_indexBuffer;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
