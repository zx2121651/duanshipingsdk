#pragma once
#ifdef __APPLE__
#include "../IBuffer.h"

namespace sdk { namespace video { namespace rhi {

class MetalBuffer final : public IBuffer {
public:
    MetalBuffer(void* mtlBuffer, size_t size);
    ~MetalBuffer() override;

    size_t   size()         const override { return m_size; }
    void*    map()                override;
    void     unmap()              override;
    uint64_t nativeHandle() const override { return reinterpret_cast<uint64_t>(m_buffer); }

private:
    void*  m_buffer = nullptr; // retained id<MTLBuffer>
    size_t m_size   = 0;
};

}}} // namespace sdk::video::rhi
#endif
