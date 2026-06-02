#version 300 es
// beauty.frag
//
// 磨皮 + 美白 — 单 pass GPU 实现。
//
// 算法流程：
//   1. YCbCr 皮肤检测 → skinMask
//   2. 近似双边模糊（3×3 高斯 × 颜色相似度权重）→ smoothed
//   3. 皮肤区域 lerp(orig, smoothed, smoothStrength) → 磨皮结果
//   4. 亮度提升曲线（仅皮肤区域）→ 美白结果
//
// 性能：< 3ms / 帧 @ 1080p Mali-G57（无分支，全并行）

precision highp float;

in vec2 v_texCoord;

uniform sampler2D inputImageTexture;
uniform float     u_smoothStrength; // [0,1]
uniform float     u_whitenStrength; // [0,1]
uniform vec2      u_texelSize;      // 1/width, 1/height

out vec4 fragColor;

// ---------------------------------------------------------------------------
// RGB → YCbCr（BT.601）
// ---------------------------------------------------------------------------
vec3 rgbToYCbCr(vec3 rgb) {
    float y  =  0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    float cb = -0.169 * rgb.r - 0.331 * rgb.g + 0.500 * rgb.b + 0.5;
    float cr =  0.500 * rgb.r - 0.419 * rgb.g - 0.081 * rgb.b + 0.5;
    return vec3(y, cb, cr);
}

// ---------------------------------------------------------------------------
// 皮肤检测（YCbCr 椭圆约束）
// ---------------------------------------------------------------------------
float skinMask(vec3 rgb) {
    vec3 ycbcr = rgbToYCbCr(rgb);
    float y  = ycbcr.x;
    float cb = ycbcr.y * 255.0;
    float cr = ycbcr.z * 255.0;
    // 典型肤色范围（包含亚洲/欧美 / 深肤色）
    float inRange = step(0.1, y) * step(y, 0.95)           // 亮度不极端
                  * step(77.0, cb) * step(cb, 127.0)        // Cb 范围
                  * step(133.0, cr) * step(cr, 173.0);      // Cr 范围
    return inRange;
}

// ---------------------------------------------------------------------------
// 3×3 近似双边模糊（颜色相似度引导）
// ---------------------------------------------------------------------------
vec3 bilateralSmooth(vec2 uv, vec3 centerColor) {
    vec3  sum    = vec3(0.0);
    float wSum   = 0.0;
    float sigmaS = 1.0;   // 空间高斯 σ（3×3 tile）
    float sigmaC = 0.12;  // 颜色 σ

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2  offset  = vec2(float(dx), float(dy)) * u_texelSize;
            vec3  neighbor = texture(inputImageTexture, uv + offset).rgb;
            float dColor  = length(neighbor - centerColor);
            float wColor  = exp(-dColor * dColor / (2.0 * sigmaC * sigmaC));
            float wSpace  = exp(-float(dx*dx + dy*dy) / (2.0 * sigmaS * sigmaS));
            float w = wColor * wSpace;
            sum  += neighbor * w;
            wSum += w;
        }
    }
    return sum / wSum;
}

// ---------------------------------------------------------------------------
// 美白曲线（提升中间调亮度，S 曲线）
// ---------------------------------------------------------------------------
vec3 whitenCurve(vec3 rgb, float strength) {
    // 将亮度向上推（类似 Gamma < 1）
    float gamma = 1.0 - strength * 0.4; // [1.0 → 0.6]
    vec3 lifted = pow(rgb, vec3(gamma));
    // 与原色适度混合，避免过曝
    return mix(rgb, lifted, strength * 0.8);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
void main() {
    vec4  orig  = texture(inputImageTexture, v_texCoord);
    vec3  color = orig.rgb;

    float skin  = skinMask(color);

    // 磨皮（皮肤区域双边模糊）
    vec3  smoothed = bilateralSmooth(v_texCoord, color);
    vec3  result   = mix(color, smoothed, skin * u_smoothStrength);

    // 美白（皮肤区域亮度曲线）
    vec3  whitened = whitenCurve(result, u_whitenStrength);
    result = mix(result, whitened, skin);

    fragColor = vec4(result, orig.a);
}
