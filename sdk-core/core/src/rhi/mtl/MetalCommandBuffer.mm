#ifdef HAS_METAL
#import <Metal/Metal.h>
#include "MetalCommandBuffer.h"
#include "MetalRenderDevice.h"
#include "MetalTexture.h"
#include "MetalBuffer.h"
#include "MetalShaderProgram.h"
#include "MetalVertexArray.h"
#include <algorithm>
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
    if (!m_cmdBuf) {
        begin();
    }
    if (desc.colorAttachments.empty() || !desc.colorAttachments[0].texture) return;
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
        switch (b.type) {
            case MetalResourceSet::BindingType::SampledTexture:
            case MetalResourceSet::BindingType::ImageTexture: {
                auto* tex = dynamic_cast<MetalTexture*>(b.texture.get());
                if (tex) [m_renderEnc setFragmentTexture:tex->mtlTexture()
                                                 atIndex:b.slot];
                break;
            }
            case MetalResourceSet::BindingType::UniformBuffer:
            case MetalResourceSet::BindingType::StorageBuffer: {
                auto* buffer = dynamic_cast<MetalBuffer*>(b.buffer.get());
                if (buffer) [m_renderEnc setFragmentBuffer:buffer->mtlBuffer()
                                                    offset:0
                                                   atIndex:b.slot];
                break;
            }
        }
    }
}

void MetalCommandBuffer::bindVertexArray(IVertexArray* vao) {
    m_currentVAO = dynamic_cast<MetalVertexArray*>(vao);
    if (!m_currentVAO || !m_renderEnc) {
        return;
    }

    const auto& vertexBuffers = m_currentVAO->vertexBuffers();
    for (uint32_t i = 0; i < vertexBuffers.size(); ++i) {
        auto* buffer = dynamic_cast<MetalBuffer*>(vertexBuffers[i].get());
        if (buffer) {
            [m_renderEnc setVertexBuffer:buffer->mtlBuffer()
                                  offset:0
                                 atIndex:i];
        }
    }
}

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
    if (!m_renderEnc || !m_currentVAO) return;
    MTLIndexType mtlIdxType = (indexType == IndexType::UInt32) ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
    MTLPrimitiveType prim = m_currentPSO
        ? toMTLPrimitive(m_currentPSO->desc.primitiveTopology)
        : MTLPrimitiveTypeTriangleStrip;
    auto indexBuffer = m_currentVAO->indexBuffer();
    auto* metalIndexBuffer = dynamic_cast<MetalBuffer*>(indexBuffer.get());
    if (!metalIndexBuffer) return;
    [m_renderEnc drawIndexedPrimitives:prim
                            indexCount:indexCount
                             indexType:mtlIdxType
                           indexBuffer:metalIndexBuffer->mtlBuffer()
                     indexBufferOffset:0];
}

void MetalCommandBuffer::pipelineBarrier(BarrierType /*type*/) {
    // Metal automatically handles resource tracking; explicit barriers rarely needed
    if (m_renderEnc)  [m_renderEnc  memoryBarrierWithScope:MTLBarrierScopeTextures
                                               afterStages:MTLRenderStageFragment
                                              beforeStages:MTLRenderStageVertex];
}

void MetalCommandBuffer::dispatchCompute(uint32_t nx, uint32_t ny, uint32_t nz) {
    if (!m_cmdBuf) {
        begin();
    }
    if (!m_computeEnc) {
        m_computeEnc = [m_cmdBuf computeCommandEncoder];
    }
    MTLSize groups   = {nx, ny, nz};
    MTLSize groupSz  = {16, 16, 1};
    [m_computeEnc dispatchThreadgroups:groups threadsPerThreadgroup:groupSz];
}

void MetalCommandBuffer::commit() {
    if (!m_cmdBuf) {
        return;
    }
    if (m_renderEnc)  { [m_renderEnc  endEncoding]; m_renderEnc  = nil; }
    if (m_computeEnc) { [m_computeEnc endEncoding]; m_computeEnc = nil; }
    [m_cmdBuf commit];
    [m_cmdBuf waitUntilCompleted];
    m_cmdBuf = nil;
}

void MetalCommandBuffer::drawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (!m_renderEnc) return;
#ifdef __OBJC__
    MTLPrimitiveType prim = m_currentPSO
        ? toMTLPrimitive(m_currentPSO->desc.primitiveTopology)
        : MTLPrimitiveTypeTriangleStrip;
    [m_renderEnc drawPrimitives:prim
                    vertexStart:firstVertex
                    vertexCount:vertexCount
                  instanceCount:instanceCount
                   baseInstance:firstInstance];
#endif
}
void MetalCommandBuffer::drawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, IndexType indexType, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!m_renderEnc || !m_currentVAO) return;
#ifdef __OBJC__
    MTLIndexType mtlIndexType = (indexType == IndexType::UInt16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
    NSUInteger indexSize = (indexType == IndexType::UInt16) ? 2 : 4;
    MTLPrimitiveType prim = m_currentPSO
        ? toMTLPrimitive(m_currentPSO->desc.primitiveTopology)
        : MTLPrimitiveTypeTriangleStrip;
    auto indexBuffer = m_currentVAO->indexBuffer();
    auto* metalIndexBuffer = dynamic_cast<MetalBuffer*>(indexBuffer.get());
    if (!metalIndexBuffer) return;
    [m_renderEnc drawIndexedPrimitives:prim
                            indexCount:indexCount
                             indexType:mtlIndexType
                           indexBuffer:metalIndexBuffer->mtlBuffer()
                     indexBufferOffset:firstIndex * indexSize
                         instanceCount:instanceCount
                            baseVertex:vertexOffset
                          baseInstance:firstInstance];
#endif
}
void MetalResourceSet::upsertBinding(Binding binding) {
    auto it = std::find_if(bindings.begin(), bindings.end(), [slot = binding.slot](const Binding& b) {
        return b.slot == slot;
    });
    if (it != bindings.end()) {
        *it = std::move(binding);
    } else {
        bindings.push_back(std::move(binding));
    }
}

void MetalResourceSet::bindTexture(uint32_t slot, std::shared_ptr<ITexture> texture) {
    auto it = std::find_if(bindings.begin(), bindings.end(), [slot](const Binding& b) { return b.slot == slot; });
    if (!texture) {
        if (it != bindings.end()) bindings.erase(it);
        return;
    }
    upsertBinding({slot, BindingType::SampledTexture, texture, nullptr, TextureAccess::Read, texture->getFormat(), 0});
}

void MetalResourceSet::bindUniformBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) {
    auto it = std::find_if(bindings.begin(), bindings.end(), [slot](const Binding& b) { return b.slot == slot; });
    if (!buffer) {
        if (it != bindings.end()) bindings.erase(it);
        return;
    }
    upsertBinding({slot, BindingType::UniformBuffer, nullptr, buffer, TextureAccess::Read, TextureFormat::RGBA8, 0});
}

void MetalResourceSet::bindStorageBuffer(uint32_t slot, std::shared_ptr<IBuffer> buffer) {
    auto it = std::find_if(bindings.begin(), bindings.end(), [slot](const Binding& b) { return b.slot == slot; });
    if (!buffer) {
        if (it != bindings.end()) bindings.erase(it);
        return;
    }
    upsertBinding({slot, BindingType::StorageBuffer, nullptr, buffer, TextureAccess::ReadWrite, TextureFormat::RGBA8, 0});
}

void MetalResourceSet::bindImageTexture(uint32_t slot, std::shared_ptr<ITexture> texture, TextureAccess access, TextureFormat format, uint32_t level) {
    auto it = std::find_if(bindings.begin(), bindings.end(), [slot](const Binding& b) { return b.slot == slot; });
    if (!texture) {
        if (it != bindings.end()) bindings.erase(it);
        return;
    }
    upsertBinding({slot, BindingType::ImageTexture, texture, nullptr, access, format, level});
}

void MetalResourceSet::apply() {
    // Metal resource binding is encoded by MetalCommandBuffer::bindResourceSet.
}

#endif // HAS_METAL

} // namespace rhi
} // namespace video
} // namespace sdk
