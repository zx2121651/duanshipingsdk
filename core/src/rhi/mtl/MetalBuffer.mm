#ifdef HAS_METAL
#import <Metal/Metal.h>
#include "MetalBuffer.h"
#include <cstring>

namespace sdk {
namespace video {
namespace rhi {

MetalBuffer::MetalBuffer(MTLDeviceRef device, BufferType, BufferUsage usage,
                         size_t size, const void* data)
    : m_size(size)
{
    MTLResourceOptions opts = (usage == BufferUsage::DynamicDraw)
        ? MTLResourceStorageModeShared   // CPU+GPU accessible
        : MTLResourceStorageModeShared;  // For simplicity; production: use Private + staging

    if (data)
        m_buffer = [device newBufferWithBytes:data length:size options:opts];
    else
        m_buffer = [device newBufferWithLength:size options:opts];
}

void* MetalBuffer::map() {
    return m_buffer ? [m_buffer contents] : nullptr;
}

void MetalBuffer::upload(const void* data, size_t size, size_t offset) {
    if (!m_buffer) return;
    std::memcpy(static_cast<uint8_t*>([m_buffer contents]) + offset, data, size);
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_METAL
