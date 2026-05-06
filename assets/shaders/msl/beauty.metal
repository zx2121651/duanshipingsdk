// beauty.metal — 磨皮 + 美白（对应 GLES beauty.frag）
#include <metal_stdlib>
using namespace metal;

struct VertexIn  { float2 position [[attribute(0)]]; float2 texCoord [[attribute(1)]]; };
struct VertexOut { float4 position [[position]]; float2 texCoord; };

vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {
    VertexOut o; o.position = float4(in.position, 0.0, 1.0); o.texCoord = in.texCoord; return o;
}

fragment float4 fragment_main(VertexOut      in          [[stage_in]],
                               texture2d<float> texInput [[texture(0)]],
                               sampler          smp      [[sampler(0)]],
                               constant float& smoothStrength [[buffer(0)]],
                               constant float& whitenStrength [[buffer(1)]])
{
    float2 uv = in.texCoord;
    float3 col = texInput.sample(smp, uv).rgb;

    // Skin detection (YCbCr range)
    float y  =  0.299f * col.r + 0.587f * col.g + 0.114f * col.b;
    float cb = -0.169f * col.r - 0.331f * col.g + 0.500f * col.b + 0.5f;
    float cr =  0.500f * col.r - 0.419f * col.g - 0.081f * col.b + 0.5f;
    bool isSkin = (y > 0.2f && y < 0.95f && cb > 0.37f && cb < 0.55f &&
                   cr > 0.53f && cr < 0.72f);

    // Bilateral-like smoothing (5-tap box as simplified proxy)
    if (isSkin && smoothStrength > 0.0f) {
        float2 step = float2(1.5f) / float2(texInput.get_width(), texInput.get_height());
        float3 blur = float3(0.0f);
        float  w    = 0.0f;
        for (int dx = -2; dx <= 2; dx++) {
            for (int dy = -2; dy <= 2; dy++) {
                float3 s = texInput.sample(smp, uv + float2(dx, dy) * step).rgb;
                float  d = dot(col - s, col - s);
                float  weight = exp(-d * 100.0f);
                blur += s * weight;
                w    += weight;
            }
        }
        col = mix(col, blur / w, smoothStrength);
    }

    // Whitening
    if (whitenStrength > 0.0f) {
        float3 brightened = float3(1.0f) - (float3(1.0f) - col) * (float3(1.0f) - col);
        col = mix(col, brightened, whitenStrength * (isSkin ? 1.0f : 0.3f));
    }

    return float4(col, 1.0f);
}
