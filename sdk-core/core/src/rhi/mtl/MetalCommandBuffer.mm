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

void MetalCommandBuffer::setViewport(float x, float y, float width, float height) {
    if (!m_renderEnc) return;
    MTLViewport vp;
    vp.originX = x; vp.originY = y; vp.width = width; vp.height = height;
    vp.znear = 0.0; vp.zfar = 1.0;
    [m_renderEnc setViewport:vp];
}

void MetalCommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    if (!m_renderEnc) return;
    MTLScissorRect rect;
    rect.x = static_cast<NSUInteger>(x); rect.y = static_cast<NSUInteger>(y);
    rect.width = width; rect.height = height;
    [m_renderEnc setScissorRect:rect];
}

static MTLPrimitiveType toMTLPrimitive(PrimitiveTopology t) {
    switch (t) {
        case PrimitiveTopology::PointList:    return MTLPrimitiveTypePoint;
        case PrimitiveTopology::LineList:     return MTLPrimitiveTypeLine;
        case PrimitiveTopology::LineStrip:    return MTLPrimitiveTypeLineStrip;
        case PrimitiveTopology::TriangleList: return MTLPrimitiveTypeTriangle;
        case PrimitiveTopology::TriangleFan:  return MTLPrimitiveTypeTriangle; // Metal has no fan; callers should triangulate
        default:                              return MTLPrimitiveTypeTriangleStrip;
    }
}

void MetalCommandBuffer::draw(uint32_t count) {
    if (!m_renderEnc) return;
    MTLPrimitiveType prim = m_currentPSO
        ? toMTLPrimitive(m_currentPSO->desc.primitiveTopology)
        : MTLPrimitiveTypeTriangleStrip;
    [m_renderEnc drawPrimitives:prim vertexStart:0 vertexCount:count];
}

void MetalCommandBuffer::drawIndexed(uint32_t indexCount, IndexType indexType) {
    if (!m_renderEnc) return;
    MTLIndexType mtlIdxType = (indexType == IndexType::UInt32) ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
    MTLPrimitiveType prim = m_currentPSO
        ? toMTLPrimitive(m_currentPSO->desc.primitiveTopology)
        : MTLPrimitiveTypeTriangleStrip;
    // Index buffer must be bound via setVertexBuffer externally; this issues the draw
    // [m_renderEnc drawIndexedPrimitives:prim indexCount:indexCount indexType:mtlIdxType indexBuffer:<buf> indexBufferOffset:0]
    (void)indexCount; (void)mtlIdxType; (void)prim; // stub until index buffer bind is wired up
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

void MetalCommandBuffer::drawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (!m_renderEnc) return;
#ifdef __OBJC__
    [m_renderEnc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:firstVertex vertexCount:vertexCount instanceCount:instanceCount baseInstance:firstInstance];
#endif
}
void MetalCommandBuffer::drawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, IndexType indexType, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!m_renderEnc) return;
#ifdef __OBJC__
    MTLIndexType mtlIndexType = (indexType == IndexType::UInt16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
    NSUInteger indexSize = (indexType == IndexType::UInt16) ? 2 : 4;
    /* Note: simplified stub for indexing */
    [m_renderEnc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:indexCount indexType:mtlIndexType indexBuffer:nil indexBufferOffset:firstIndex*indexSize instanceCount:instanceCount baseVertex:vertexOffset baseInstance:firstInstance];
#endif
}
void MetalResourceSet::bindStorageBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) {
}
void MetalResourceSet::bindImageTexture(uint32_t slot, std::shared_ptr<ITexture> texture, TextureAccess access, TextureFormat format, uint32_t level) {
}

#endif // HAS_METAL

} // namespace rhi
} // namespace video
} // namespace sdk
