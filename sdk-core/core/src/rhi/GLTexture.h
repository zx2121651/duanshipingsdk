#pragma once
#include "../../include/rhi/ITexture.h"
#include "../../include/GLTypes.h" // For Texture struct compatibility initially

namespace sdk {
namespace video {
namespace rhi {

class GLRenderDevice; // forward declaration for friend

class GLTexture : public ITexture {
public:
    GLTexture(uint32_t id, uint32_t width, uint32_t height, TextureFormat fmt = TextureFormat::RGBA8, uint32_t mipLevels = 1)
        : m_id(id), m_width(width), m_height(height), m_format(fmt), m_mipLevels(mipLevels), m_ownsHandle(false) {}

    ~GLTexture() override;

    uint32_t getWidth() const override { return m_width; }
    uint32_t getHeight() const override { return m_height; }
    uint32_t getId() const override { return m_id; }
    TextureFormat getFormat() const override { return m_format; }

    uint32_t getMipLevels() const { return m_mipLevels; }

private:
    // Only the factory (GLRenderDevice) may set ownership to prevent double-free
    friend class GLRenderDevice;
    void setOwnsHandle(bool owns) { m_ownsHandle = owns; }

    uint32_t m_id;
    uint32_t m_width;
    uint32_t m_height;
    TextureFormat m_format;
    uint32_t m_mipLevels;
    bool m_ownsHandle;
};

} // namespace rhi
} // namespace video
} // namespace sdk
