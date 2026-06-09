#pragma once
#ifdef HAS_METAL

#include "../../../include/rhi/IBuffer.h"
#ifdef __OBJC__
#   import <Metal/Metal.h>
    using MTLDeviceRef = id<MTLDevice>;
    using MTLBufferRef = id<MTLBuffer>;
#else
    using MTLDeviceRef = void*;
    using MTLBufferRef = void*;
#endif

namespace sdk {
namespace video {
namespace rhi {

class MetalBuffer : public IBuffer {
public:
    MetalBuffer(MTLDeviceRef device, BufferType type, BufferUsage usage,
                size_t size, const void* data);
    ~MetalBuffer() override = default;

    BufferType getType() const override { return m_type; }
    size_t getSize() const override { return m_size; }
    void updateData(const void* data, size_t size, size_t offset = 0) override;
    void* map(size_t offset, size_t size, BufferAccess access) override;
    void   unmap() override {}

    MTLBufferRef mtlBuffer() const { return m_buffer; }

private:
    MTLBufferRef m_buffer = nullptr;
    BufferType   m_type   = BufferType::VertexBuffer;
    size_t       m_size   = 0;
};

} // namespace rhi
} // namespace video
} // namespace sdk

#endif // HAS_METAL
