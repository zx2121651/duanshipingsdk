#version 300 es
/**
 * hair_color.frag — 发色染色着色器
 * 对标抖音染发特效：色相替换 + 保留亮度/饱和度质感
 */
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_inputTexture;
uniform sampler2D u_hairMask;     // R 通道: 1=头发区域
uniform vec3      u_hairColor;    // 目标发色 [0,1]
uniform float     u_colorIntensity;
uniform float     u_glossIntensity;
out vec4 fragColor;

// RGB ↔ HSL 互转
vec3 rgb2hsl(vec3 c) {
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float d    = maxC - minC;
    float l    = (maxC + minC) * 0.5;
    float s    = (d < 0.001) ? 0.0 : d / (1.0 - abs(2.0*l - 1.0));
    float h    = 0.0;
    if (d > 0.001) {
        if      (maxC == c.r) h = mod((c.g - c.b) / d, 6.0);
        else if (maxC == c.g) h = (c.b - c.r) / d + 2.0;
        else                  h = (c.r - c.g) / d + 4.0;
        h /= 6.0;
    }
    return vec3(h, s, l);
}

vec3 hsl2rgb(vec3 hsl) {
    float h = hsl.x, s = hsl.y, l = hsl.z;
    float c = (1.0 - abs(2.0*l - 1.0)) * s;
    float x = c * (1.0 - abs(mod(h * 6.0, 2.0) - 1.0));
    float m = l - c * 0.5;
    vec3 rgb;
    int hi = int(h * 6.0);
    if      (hi == 0) rgb = vec3(c, x, 0);
    else if (hi == 1) rgb = vec3(x, c, 0);
    else if (hi == 2) rgb = vec3(0, c, x);
    else if (hi == 3) rgb = vec3(0, x, c);
    else if (hi == 4) rgb = vec3(x, 0, c);
    else              rgb = vec3(c, 0, x);
    return rgb + vec3(m);
}

void main() {
    vec4 orig  = texture(u_inputTexture, v_texCoord);
    float mask = texture(u_hairMask, v_texCoord).r;
    mask = smoothstep(0.35, 0.65, mask); // 边缘软化

    // 原始发色 HSL
    vec3 origHSL  = rgb2hsl(orig.rgb);

    // 目标发色 HSL（只替换色相和部分饱和度，保留亮度）
    vec3 targetHSL = rgb2hsl(u_hairColor);

    // 新色相 = 目标色相，饱和度加权，亮度保持原始
    vec3 newHSL = vec3(
        targetHSL.x,
        mix(origHSL.y, max(origHSL.y, targetHSL.y), 0.6),
        origHSL.z
    );
    vec3 recolored = hsl2rgb(newHSL);

    // 高光叠加（提升头发光泽感）
    float gloss = 0.0;
    if (u_glossIntensity > 0.001) {
        // 亮度较高的区域（高光位置）
        gloss = smoothstep(0.65, 0.9, origHSL.z) * u_glossIntensity * mask;
        recolored = min(recolored + vec3(gloss * 0.3), vec3(1.0));
    }

    vec3 result = mix(orig.rgb, recolored, mask * u_colorIntensity);
    fragColor   = vec4(result, orig.a);
}
