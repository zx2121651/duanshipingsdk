#version 300 es
// beauty.frag
// Single-pass skin smoothing + whitening. Keep this in sync with BeautyFilter.cpp.
precision highp float;

in vec2 v_texCoord;

uniform sampler2D inputImageTexture;
uniform float     u_smoothStrength; // [0,1]
uniform float     u_whitenStrength; // [0,1]
uniform vec2      u_texelSize;      // 1/width, 1/height
uniform int       u_hasFace;
uniform vec2      u_faceCenter;
uniform vec2      u_faceRadius;

out vec4 fragColor;

vec3 rgbToYCbCr(vec3 rgb) {
    float y  =  0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    float cb = -0.169 * rgb.r - 0.331 * rgb.g + 0.500 * rgb.b + 0.5;
    float cr =  0.500 * rgb.r - 0.419 * rgb.g - 0.081 * rgb.b + 0.5;
    return vec3(y, cb, cr);
}

float skinMask(vec3 rgb) {
    vec3 ycbcr = rgbToYCbCr(rgb);
    float y  = ycbcr.x;
    float cb = ycbcr.y * 255.0;
    float cr = ycbcr.z * 255.0;
    float inRange = step(0.1, y) * step(y, 0.95)
                  * step(77.0, cb) * step(cb, 127.0)
                  * step(133.0, cr) * step(cr, 173.0);
    return inRange;
}

float faceMask(vec2 uv) {
    if (u_hasFace == 0) return 1.0;
    vec2 radius = max(u_faceRadius, vec2(0.001));
    vec2 d = (uv - u_faceCenter) / radius;
    float dist = dot(d, d);
    return smoothstep(1.0, 0.6, dist);
}

vec3 bilateralSmooth(vec2 uv, vec3 centerColor) {
    vec3  sum  = vec3(0.0);
    float wSum = 0.0;
    float sigmaS = 1.0;
    float sigmaC = 0.12;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 offset = vec2(float(dx), float(dy)) * u_texelSize;
            vec3 neighbor = texture(inputImageTexture, uv + offset).rgb;
            float dColor = length(neighbor - centerColor);
            float wColor = exp(-dColor * dColor / (2.0 * sigmaC * sigmaC));
            float wSpace = exp(-float(dx * dx + dy * dy) / (2.0 * sigmaS * sigmaS));
            float w = wColor * wSpace;
            sum += neighbor * w;
            wSum += w;
        }
    }
    return sum / max(wSum, 0.0001);
}

vec3 whitenCurve(vec3 rgb, float strength) {
    float gamma = 1.0 - strength * 0.4;
    vec3 lifted = pow(rgb, vec3(gamma));
    return mix(rgb, lifted, strength * 0.8);
}

void main() {
    vec4 orig = texture(inputImageTexture, v_texCoord);
    vec3 color = orig.rgb;

    float skin = skinMask(color) * faceMask(v_texCoord);
    vec3 smoothed = bilateralSmooth(v_texCoord, color);
    vec3 result = mix(color, smoothed, skin * u_smoothStrength);
    result = mix(result, whitenCurve(result, u_whitenStrength), skin);

    fragColor = vec4(result, orig.a);
}
