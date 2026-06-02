#version 300 es
precision highp float;

// Dual Kawase blur — single pass with configurable diagonal offset.
// Calling this N times with offsets 0.5, 1.5, ... (N-0.5) * blurOffset
// produces a high-quality large-radius blur at a fraction of Gaussian cost.

in vec2 v_texCoord;

uniform sampler2D inputImageTexture;
uniform vec2      u_texelSize;   // vec2(1.0/width, 1.0/height)
uniform float     u_offset;      // current pass offset in texels

out vec4 fragColor;

void main() {
    float d = u_offset;
    vec4 sum  = texture(inputImageTexture, v_texCoord + vec2(-d, -d) * u_texelSize);
    sum      += texture(inputImageTexture, v_texCoord + vec2(-d,  d) * u_texelSize);
    sum      += texture(inputImageTexture, v_texCoord + vec2( d, -d) * u_texelSize);
    sum      += texture(inputImageTexture, v_texCoord + vec2( d,  d) * u_texelSize);
    fragColor = sum * 0.25;
}
