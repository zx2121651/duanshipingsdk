#ifdef HAS_METAL
#import <Metal/Metal.h>
#include "MetalBuffer.h"
#include <cstring>
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

MetalBuffer::MetalBuffer(MTLDeviceRef device, BufferType type, BufferUsage usage,
                         size_t size, const void* data)
    : m_type(type), m_size(size)
{
    MTLResourceOptions opts = (usage == BufferUsage::DynamicDraw)
        ? MTLResourceStorageModeShared   // CPU+GPU accessible
        : MTLResourceStorageModeShared;  // For simplicity; production: use Private + staging

    if (data)
        m_buffer = [device newBufferWithBytes:data length:size options:opts];
    else
        m_buffer = [device newBufferWithLength:size options:opts];
}

void* MetalBuffer::map(size_t offset, size_t size, BufferAccess /*access*/) {
    if (!m_buffer) return nullptr;
    if (offset > m_size) {
        std::cerr << "MetalBuffer::map: range exceeds buffer size" << std::endl;
        return nullptr;
    }
    const size_t mapSize = size == 0 ? m_size - offset : size;
    if (mapSize > m_size - offset) {
        std::cerr << "MetalBuffer::map: range exceeds buffer size" << std::endl;
        return nullptr;
    }
    return static_cast<uint8_t*>([m_buffer contents]) + offset;
}

void MetalBuffer::updateData(const void* data, size_t size, size_t offset) {
    if (!m_buffer || !data || size == 0) return;
    if (offset > m_size || size > m_size - offset) {
        std::cerr << "MetalBuffer::updateData: range exceeds buffer size" << std::endl;
        return;
    }
    std::memcpy(static_cast<uint8_t*>([m_buffer contents]) + offset, data, size);
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_METAL
