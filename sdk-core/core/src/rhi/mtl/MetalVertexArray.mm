#ifdef HAS_METAL
#include "MetalVertexArray.h"

namespace sdk {
namespace video {
namespace rhi {

void MetalVertexArray::addVertexBuffer(std::shared_ptr<IBuffer> vertexBuffer,
                                       const std::vector<VertexAttribute>& attributes) {
    if (!vertexBuffer || vertexBuffer->getType() != BufferType::VertexBuffer) {
        return;
    }

    m_vertexBuffers.push_back(std::move(vertexBuffer));
    m_vertexAttributes.push_back(attributes);
}

void MetalVertexArray::setIndexBuffer(std::shared_ptr<IBuffer> indexBuffer) {
    if (!indexBuffer || indexBuffer->getType() != BufferType::IndexBuffer) {
        return;
    }

    m_indexBuffer = std::move(indexBuffer);
}

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_METAL
