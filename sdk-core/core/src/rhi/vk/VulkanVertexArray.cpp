#ifdef HAS_VULKAN
#include "VulkanVertexArray.h"

namespace sdk {
namespace video {
namespace rhi {

void VulkanVertexArray::addVertexBuffer(std::shared_ptr<IBuffer> vertexBuffer,
                                        const std::vector<VertexAttribute>& attributes) {
    if (!vertexBuffer || vertexBuffer->getType() != BufferType::VertexBuffer) {
        return;
    }

    m_vertexBuffers.push_back(std::move(vertexBuffer));
    m_vertexAttributes.push_back(attributes);
}

void VulkanVertexArray::setIndexBuffer(std::shared_ptr<IBuffer> indexBuffer) {
    if (!indexBuffer || indexBuffer->getType() != BufferType::IndexBuffer) {
        return;
    }

    m_indexBuffer = std::move(indexBuffer);
}

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_VULKAN
