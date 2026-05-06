#ifdef HAS_METAL
#import <Metal/Metal.h>
#include "MetalShaderProgram.h"
#include <iostream>
#include <cstring>

namespace sdk {
namespace video {
namespace rhi {

static id<MTLFunction> compileFunction(id<MTLDevice> device,
                                        const char* mslSrc,
                                        const char* entryPoint)
{
    NSError* err = nil;
    NSString* src = [NSString stringWithUTF8String:mslSrc];
    id<MTLLibrary> lib = [device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        NSLog(@"MetalShaderProgram compile error: %@", err.localizedDescription);
        return nil;
    }
    NSString* ep = [NSString stringWithUTF8String:entryPoint];
    id<MTLFunction> fn = [lib newFunctionWithName:ep];
    if (!fn) NSLog(@"MetalShaderProgram: entry point '%s' not found", entryPoint);
    return fn;
}

std::shared_ptr<MetalShaderProgram> MetalShaderProgram::createFromMSL(
    MTLDeviceRef device, const char* vertMSL, const char* fragMSL)
{
    auto prog = std::shared_ptr<MetalShaderProgram>(new MetalShaderProgram());
    prog->m_uniformData.resize(256, 0);
    prog->m_vertFunction = compileFunction(device, vertMSL, "vertex_main");
    prog->m_fragFunction = compileFunction(device, fragMSL, "fragment_main");
    return prog;
}

std::shared_ptr<MetalShaderProgram> MetalShaderProgram::createTessellationFromMSL(
    MTLDeviceRef device, const char* kernelMSL, const char* fragMSL)
{
    auto prog = std::shared_ptr<MetalShaderProgram>(new MetalShaderProgram());
    prog->m_uniformData.resize(256, 0);
    prog->m_vertFunction = compileFunction(device, kernelMSL, "tessellation_kernel");
    prog->m_fragFunction = compileFunction(device, fragMSL,   "fragment_main");
    return prog;
}

void MetalShaderProgram::registerUniform(const std::string& name, uint32_t size) {
    if (m_uniformOffsets.count(name)) return;
    if (m_uniformCursor + size > 256) { std::cerr << "MetalShaderProgram: uniform buffer overflow" << std::endl; return; }
    m_uniformOffsets[name] = m_uniformCursor;
    m_uniformCursor += size;
}

template<typename T>
void MetalShaderProgram::writeUniform(const std::string& name, const T& val) {
    registerUniform(name, static_cast<uint32_t>(sizeof(T)));
    auto it = m_uniformOffsets.find(name);
    if (it != m_uniformOffsets.end())
        std::memcpy(m_uniformData.data() + it->second, &val, sizeof(T));
}

void MetalShaderProgram::setUniform1i(const std::string& n, int v)   { writeUniform(n, v); }
void MetalShaderProgram::setUniform1f(const std::string& n, float v) { writeUniform(n, v); }
void MetalShaderProgram::setUniform2f(const std::string& n, float x, float y) {
    float v[2] = {x, y};
    registerUniform(n, 8);
    auto it = m_uniformOffsets.find(n);
    if (it != m_uniformOffsets.end()) std::memcpy(m_uniformData.data() + it->second, v, 8);
}
void MetalShaderProgram::setUniform4f(const std::string& n, float x, float y, float z, float w) {
    float v[4] = {x, y, z, w};
    registerUniform(n, 16);
    auto it = m_uniformOffsets.find(n);
    if (it != m_uniformOffsets.end()) std::memcpy(m_uniformData.data() + it->second, v, 16);
}
void MetalShaderProgram::setUniformMat4(const std::string& n, const float* m) {
    registerUniform(n, 64);
    auto it = m_uniformOffsets.find(n);
    if (it != m_uniformOffsets.end()) std::memcpy(m_uniformData.data() + it->second, m, 64);
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_METAL
