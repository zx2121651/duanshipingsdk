// passthrough.metal — 通用全屏四边形直通着色器（对应 GLES default.vert + record_passthrough.frag）
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position  [[attribute(0)]];
    float2 texCoord  [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    out.texCoord = in.texCoord;
    return out;
}

fragment float4 fragment_main(VertexOut in    [[stage_in]],
                               texture2d<float> tex [[texture(0)]],
                               sampler          smp [[sampler(0)]]) {
    return tex.sample(smp, in.texCoord);
}
