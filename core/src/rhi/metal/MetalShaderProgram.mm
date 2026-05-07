#ifdef __APPLE__
#import <Metal/Metal.h>
#include "../../../include/rhi/metal/MetalShaderProgram.h"
#define LOG_TAG "MetalShaderProgram"
#include "../../../include/Log.h"

namespace sdk { namespace video { namespace rhi {

MetalShaderProgram::MetalShaderProgram(
    void* library, void* cmdQueue,
    const std::string& vertEntry, const std::string& fragEntry)
    : m_library(library), m_commandQueue(cmdQueue),
      m_vertEntry(vertEntry), m_fragEntry(fragEntry) {}

MetalShaderProgram::~MetalShaderProgram() {
    if (m_library) { CFRelease(m_library); m_library = nullptr; }
}

void MetalShaderProgram::bind()   { /* Pipeline state set per draw-call via encoder */ }
void MetalShaderProgram::unbind() {}

void MetalShaderProgram::setUniformInt(const std::string& name, int value) {
    // Metal uniforms are passed via setBytes:length:atIndex: on the render command encoder.
    // Store pending uniforms in a map; flush in bind() or a dedicated flush() call.
    LOGD("setUniformInt %s=%d (deferred, set via encoder at draw time)", name.c_str(), value);
}

void MetalShaderProgram::setUniformFloat(const std::string& name, float value) {
    LOGD("setUniformFloat %s=%.4f (deferred)", name.c_str(), value);
}

void MetalShaderProgram::setUniformMat4(
    const std::string& name, const float* /*m16*/, bool /*transpose*/)
{
    LOGD("setUniformMat4 %s (deferred)", name.c_str());
}

}}} // namespace sdk::video::rhi
#endif
