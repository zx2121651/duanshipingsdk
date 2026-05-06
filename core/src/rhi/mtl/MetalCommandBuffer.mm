#ifdef HAS_METAL
#import <Metal/Metal.h>
#include "MetalCommandBuffer.h"
#include "MetalRenderDevice.h"
#include "MetalTexture.h"
#include "MetalBuffer.h"
#include "MetalShaderProgram.h"
#include <iostream>

namespace sdk {
namespace video {
namespace rhi {

MetalCommandBuffer::MetalCommandBuffer(MetalRenderDevice* device)
    : m_rhiDevice(device) {}

void MetalCommandBuffer::begin() {
    id<MTLCommandQueue> queue = m_rhiDevice->mtlCommandQueue();
    m_cmdBuf = [queue commandBuffer];
}

void MetalCommandBuffer::end() {
    if (m_renderEnc)  { [m_renderEnc  endEncoding]; m_renderEnc  = nil; }
    if (m_computeEnc) { [m_computeEnc endEncoding]; m_computeEnc = nil; }
}

// ---------------------------------------------------------------------------
void MetalCommandBuffer::beginRenderPass(const RenderPassDescriptor& desc) {
    if (desc.colorAttachments.empty()) return;
    auto* tex = dynamic_cast<MetalTexture*>(desc.colorAttachments[0].texture.get());
    if (!tex) return;

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor new];
    auto& ca = desc.colorAttachments[0];
    rpd.colorAttachments[0].texture     = tex->mtlTexture();
    rpd.colorAttachments[0].loadAction  = (ca.loadAction == LoadAction::Clear)
        ? MTLLoadActionClear : MTLLoadActionLoad;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor  =
        MTLClearColorMake(ca.clearColor.r, ca.clearColor.g,
                          ca.clearColor.b, ca.clearColor.a);

    if (desc.hasDepthStencil && desc.depthStencilAttachment.texture) {
        auto* dtex = dynamic_cast<MetalTexture*>(
            desc.depthStencilAttachment.texture.get());
        if (dtex) {
            rpd.depthAttachment.texture    = dtex->mtlTexture();
            rpd.depthAttachment.loadAction = MTLLoadActionClear;
            rpd.depthAttachment.clearDepth = 1.0;
        }
    }
    m_renderEnc = [m_cmdBuf renderCommandEncoderWithDescriptor:rpd];
}

void MetalCommandBuffer::endRenderPass() {
    if (m_renderEnc) { [m_renderEnc endEncoding]; m_renderEnc = nil; }
}

// ---------------------------------------------------------------------------
void MetalCommandBuffer::bindPipelineState(std::shared_ptr<IPipelineState> pso) {
    m_currentPSO = std::dynamic_pointer_cast<MetalPipelineState>(pso);
    if (!m_currentPSO || !m_renderEnc) return;

    // Build MTLRenderPipelineState lazily
    if (!m_currentPSO->mtlPSO) {
        auto* prog = dynamic_cast<MetalShaderProgram*>(
            m_currentPSO->desc.shaderProgram);
        if (!prog) return;

        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction   = prog->vertFunction();
        pd.fragmentFunction = prog->fragFunction();
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

        // Blend state
        auto& bs = m_currentPSO->desc.blendState;
        pd.colorAttachments[0].blendingEnabled = bs.blendEnabled;
        if (bs.blendEnabled) {
            pd.colorAttachments[0].sourceRGBBlendFactor      = MTLBlendFactorSourceAlpha;
            pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            pd.colorAttachments[0].sourceAlphaBlendFactor    = MTLBlendFactorOne;
            pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        }

        NSError* err = nil;
        m_currentPSO->mtlPSO =
            [m_rhiDevice->mtlDevice() newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!m_currentPSO->mtlPSO)
            NSLog(@"MetalCommandBuffer: pipeline compile error: %@", err.localizedDescription);
    }
    if (m_currentPSO->mtlPSO)
        [m_renderEnc setRenderPipelineState:m_currentPSO->mtlPSO];
}

void MetalCommandBuffer::bindResourceSet(
    uint32_t /*setIndex*/, std::shared_ptr<IShaderResourceSet> rs)
{
    m_currentRS = std::dynamic_pointer_cast<MetalResourceSet>(rs);
    if (!m_currentRS || !m_renderEnc) return;
    for (auto& b : m_currentRS->bindings) {
        auto* tex = dynamic_cast<MetalTexture*>(b.texture.get());
        if (tex) [m_renderEnc setFragmentTexture:tex->mtlTexture()
                                         atIndex:b.slot];
    }
}

void MetalCommandBuffer::bindVertexArray(IVertexArray* /*vao*/) {}

void MetalCommandBuffer::draw(uint32_t count) {
    if (m_renderEnc)
        [m_renderEnc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                        vertexStart:0
                        vertexCount:count];
}

void MetalCommandBuffer::drawIndexed(uint32_t /*indexCount*/) {
    // TODO: [m_renderEnc drawIndexedPrimitives:...]
}

void MetalCommandBuffer::pipelineBarrier(BarrierType /*type*/) {
    // Metal automatically handles resource tracking; explicit barriers rarely needed
    if (m_renderEnc)  [m_renderEnc  memoryBarrierWithScope:MTLBarrierScopeTextures
                                               afterStages:MTLRenderStageFragment
                                              beforeStages:MTLRenderStageVertex];
}

void MetalCommandBuffer::dispatchCompute(uint32_t nx, uint32_t ny, uint32_t nz) {
    if (!m_computeEnc) {
        m_computeEnc = [m_cmdBuf computeCommandEncoder];
    }
    MTLSize groups   = {nx, ny, nz};
    MTLSize groupSz  = {16, 16, 1};
    [m_computeEnc dispatchThreadgroups:groups threadsPerThreadgroup:groupSz];
}

void MetalCommandBuffer::commit() {
    if (m_renderEnc)  { [m_renderEnc  endEncoding]; m_renderEnc  = nil; }
    if (m_computeEnc) { [m_computeEnc endEncoding]; m_computeEnc = nil; }
    [m_cmdBuf commit];
    [m_cmdBuf waitUntilCompleted];
}

} // namespace rhi
} // namespace video
} // namespace sdk
#endif // HAS_METAL
