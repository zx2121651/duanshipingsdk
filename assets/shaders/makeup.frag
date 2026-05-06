#version 300 es
/**
 * makeup.frag — 多层 AI 美妆着色器
 * 基于人脸关键点生成各部位蒙版，再叠加对应颜色。
 * 对标抖音：口红/腮红/眼影/高光/修容/眉毛
 */
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_inputTexture;

uniform bool  u_hasFace;
uniform vec2  u_landmarks[106];

// 各妆容层
uniform vec3  u_lipColor;
uniform float u_lipIntensity;
uniform vec3  u_blushColor;
uniform float u_blushIntensity;
uniform vec3  u_eyeColor;
uniform float u_eyeIntensity;
uniform float u_highlight;  // 高光强度
uniform float u_contour;    // 修容强度
uniform vec3  u_eyebrowColor;
uniform float u_eyebrowIntensity;

out vec4 fragColor;

// ── 距离衰减蒙版 ──────────────────────────────────────────────────────────
float gaussMask(vec2 uv, vec2 center, float radius) {
    float d = length(uv - center);
    return exp(-d * d / (2.0 * radius * radius));
}

// ── 混合模式 ──────────────────────────────────────────────────────────────
vec3 blendOverlay(vec3 base, vec3 blend) {
    return mix(
        2.0 * base * blend,
        1.0 - 2.0 * (1.0 - base) * (1.0 - blend),
        step(0.5, base));
}
vec3 blendScreen(vec3 base, vec3 blend) {
    return 1.0 - (1.0 - base) * (1.0 - blend);
}
vec3 blendMultiply(vec3 base, vec3 blend) {
    return base * blend;
}

void main() {
    vec4 orig = texture(u_inputTexture, v_texCoord);
    vec3 color = orig.rgb;

    if (!u_hasFace) {
        fragColor = orig;
        return;
    }

    // ── 口红 ─────────────────────────────────────────────────────────────
    if (u_lipIntensity > 0.001) {
        // 嘴部区域：landmarks [48..67] 中心
        vec2 lipCenter = vec2(0.0);
        for (int i = 48; i < 68; ++i) lipCenter += u_landmarks[i];
        lipCenter /= 20.0;

        // 嘴宽/高估算
        float mouthW = length(u_landmarks[54] - u_landmarks[48]) * 0.5;
        float mouthH = length(u_landmarks[57] - u_landmarks[51]) * 0.6;
        float mw = max(mouthW, 0.04);
        float mh = max(mouthH, 0.025);

        // 椭圆蒙版
        vec2 d = (v_texCoord - lipCenter) / vec2(mw, mh);
        float lipMask = smoothstep(1.0, 0.3, dot(d, d));

        // 皮肤颜色检测（只在嘴唇区域叠加）
        vec3 overlaid = blendOverlay(color, u_lipColor);
        color = mix(color, overlaid, lipMask * u_lipIntensity);
    }

    // ── 腮红 ─────────────────────────────────────────────────────────────
    if (u_blushIntensity > 0.001) {
        // 左右颧骨区域（landmarks 1, 15）
        float blushR  = 0.08;
        float blushL  = gaussMask(v_texCoord, u_landmarks[1],  blushR);
        float blushRt = gaussMask(v_texCoord, u_landmarks[15], blushR);
        float blushMask = clamp(blushL + blushRt, 0.0, 1.0) * 0.85;

        vec3 screened = blendScreen(color, u_blushColor);
        color = mix(color, screened, blushMask * u_blushIntensity);
    }

    // ── 眼影 ─────────────────────────────────────────────────────────────
    if (u_eyeIntensity > 0.001) {
        // 右眼上方（landmarks 37,38），左眼上方（43,44）
        vec2 eyeR = (u_landmarks[37] + u_landmarks[38]) * 0.5;
        vec2 eyeL = (u_landmarks[43] + u_landmarks[44]) * 0.5;
        float eyeR2 = 0.05;
        float em = gaussMask(v_texCoord, eyeR, eyeR2)
                 + gaussMask(v_texCoord, eyeL, eyeR2);
        em = clamp(em, 0.0, 1.0) * 0.9;
        vec3 eyeBlend = blendMultiply(color, mix(vec3(1.0), u_eyeColor, 0.8));
        color = mix(color, eyeBlend, em * u_eyeIntensity);
    }

    // ── 高光 ─────────────────────────────────────────────────────────────
    if (u_highlight > 0.001) {
        // 额头中心（landmarks 27 向上偏移）、鼻梁（30）
        vec2 hlPts[3];
        hlPts[0] = u_landmarks[27] + vec2(0.0, -0.06);
        hlPts[1] = u_landmarks[30];
        hlPts[2] = u_landmarks[27] + vec2(0.0, -0.08);
        float hm = 0.0;
        for (int i = 0; i < 3; ++i) hm += gaussMask(v_texCoord, hlPts[i], 0.035);
        hm = clamp(hm, 0.0, 1.0);
        color = mix(color, min(color + vec3(0.25), vec3(1.0)), hm * u_highlight);
    }

    // ── 修容（侧脸阴影）─────────────────────────────────────────────────
    if (u_contour > 0.001) {
        // 颧骨外侧 + 额头两侧
        vec2 cL  = u_landmarks[0]  + vec2(-0.02, 0.0);
        vec2 cR  = u_landmarks[16] + vec2( 0.02, 0.0);
        float cm = gaussMask(v_texCoord, cL, 0.06)
                 + gaussMask(v_texCoord, cR, 0.06);
        cm = clamp(cm, 0.0, 1.0);
        vec3 contourColor = vec3(0.55, 0.38, 0.28); // 暗棕色
        vec3 multiplied   = blendMultiply(color, contourColor);
        color = mix(color, multiplied, cm * u_contour * 0.7);
    }

    // ── 眉毛 ─────────────────────────────────────────────────────────────
    if (u_eyebrowIntensity > 0.001) {
        vec2 brR = vec2(0.0); for (int i = 17; i < 22; ++i) brR += u_landmarks[i]; brR /= 5.0;
        vec2 brL = vec2(0.0); for (int i = 22; i < 27; ++i) brL += u_landmarks[i]; brL /= 5.0;
        float bm = gaussMask(v_texCoord, brR, 0.025)
                 + gaussMask(v_texCoord, brL, 0.025);
        bm = clamp(bm * 2.0, 0.0, 1.0);
        // 眉毛使用 Normal blend（直接用颜色覆盖）
        color = mix(color, u_eyebrowColor, bm * u_eyebrowIntensity * 0.75);
    }

    fragColor = vec4(color, orig.a);
}
