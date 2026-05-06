// segmentation.metal — 人像分割合成（对应 GLES segmentation.frag）
#include <metal_stdlib>
using namespace metal;

struct VertexIn  { float2 position [[attribute(0)]]; float2 texCoord [[attribute(1)]]; };
struct VertexOut { float4 position [[position]]; float2 texCoord; };

vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {
    VertexOut o; o.position = float4(in.position, 0.0, 1.0); o.texCoord = in.texCoord; return o;
}

fragment float4 fragment_main(VertexOut        in         [[stage_in]],
                               texture2d<float> texFg     [[texture(0)]],  // 前景（原始帧）
                               texture2d<float> texMask   [[texture(1)]],  // 分割 mask（R 通道）
                               texture2d<float> texBg     [[texture(2)]],  // 背景替换纹理
                               sampler          smp       [[sampler(0)]],
                               constant int&    bgMode    [[buffer(0)]],   // 0=blur 1=replace 2=color
                               constant float4& bgColor   [[buffer(1)]],   // bgMode==2 时的颜色
                               constant float&  edgeFade  [[buffer(2)]])
{
    float2 uv = in.texCoord;
    float mask = texMask.sample(smp, uv).r;
    mask = smoothstep(0.4f - edgeFade, 0.6f + edgeFade, mask);

    float4 fg = texFg.sample(smp, uv);

    float4 bg;
    if (bgMode == 1) {
        bg = texBg.sample(smp, uv);
    } else if (bgMode == 2) {
        bg = bgColor;
    } else {
        // Blur: simple 9-tap box
        float2 step = float2(3.0f) / float2(texFg.get_width(), texFg.get_height());
        float4 blurred = float4(0.0f);
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                blurred += texFg.sample(smp, uv + float2(dx, dy) * step);
        bg = blurred / 9.0f;
    }

    return mix(bg, fg, mask);
}
