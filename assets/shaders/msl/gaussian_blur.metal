// gaussian_blur.metal — 高斯模糊两 pass（对应 GLES gaussian_blur.vert/frag）
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};
struct VertexOut {
    float4 position  [[position]];
    float2 blurCoord[5];
};

// 水平 pass
vertex VertexOut vertex_blur_h(VertexIn in [[stage_in]],
                                constant float2& texelSize [[buffer(0)]])
{
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    float2 tc = in.texCoord;
    for (int i = -2; i <= 2; ++i)
        out.blurCoord[i + 2] = tc + float2(texelSize.x * float(i), 0.0);
    return out;
}

// 垂直 pass
vertex VertexOut vertex_blur_v(VertexIn in [[stage_in]],
                                constant float2& texelSize [[buffer(0)]])
{
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    float2 tc = in.texCoord;
    for (int i = -2; i <= 2; ++i)
        out.blurCoord[i + 2] = tc + float2(0.0, texelSize.y * float(i));
    return out;
}

constant float kWeights[5] = {0.0625, 0.25, 0.375, 0.25, 0.0625};

fragment float4 fragment_main(VertexOut in [[stage_in]],
                               texture2d<float> tex [[texture(0)]],
                               sampler          smp [[sampler(0)]])
{
    float4 color = float4(0.0);
    for (int i = 0; i < 5; ++i)
        color += tex.sample(smp, in.blurCoord[i]) * kWeights[i];
    return color;
}
