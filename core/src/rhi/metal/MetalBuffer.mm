#ifdef __APPLE__
#import <Metal/Metal.h>
#include "../../../include/rhi/metal/MetalBuffer.h"

namespace sdk { namespace video { namespace rhi {

MetalBuffer::MetalBuffer(void* buf, size_t sz)
    : m_buffer(buf), m_size(sz) {}

MetalBuffer::~MetalBuffer() {
    if (m_buffer) { CFRelease(m_buffer); m_buffer = nullptr; }
}

void* MetalBuffer::map() {
    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)m_buffer;
    return [buf contents];
}

void MetalBuffer::unmap() {
    // MTLResourceStorageModeShared buffers don't need explicit flush on Apple Silicon.
    // For Intel Mac / A-series with managed storage, call didModifyRange: here.
}

}}} // namespace sdk::video::rhi
#endif
