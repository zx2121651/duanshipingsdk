#version 300 es
precision highp float;

// Bloom — additive composite pass.
// Adds the blurred bloom layer back onto the original frame.

in vec2 v_texCoord;

uniform sampler2D inputImageTexture;  // original frame
uniform sampler2D u_bloomTex;         // blurred bright areas
uniform float     u_intensity;        // bloom brightness multiplier

out vec4 fragColor;

void main() {
    vec3 orig  = texture(inputImageTexture, v_texCoord).rgb;
    vec3 bloom = texture(u_bloomTex,        v_texCoord).rgb;
    fragColor  = vec4(clamp(orig + bloom * u_intensity, 0.0, 1.0), 1.0);
}
