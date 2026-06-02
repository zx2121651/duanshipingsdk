#version 300 es
precision highp float;

// Bloom — threshold extraction pass.
// Extracts pixels whose luminance exceeds u_threshold using a soft-knee curve.

in vec2 v_texCoord;

uniform sampler2D inputImageTexture;
uniform float     u_threshold;  // luminance cutoff  [0, 1], e.g. 0.8
uniform float     u_knee;       // soft-knee width   [0, 0.5], e.g. 0.1

out vec4 fragColor;

void main() {
    vec3  color      = texture(inputImageTexture, v_texCoord).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft-knee: smoothly ramp extraction near the threshold
    float rq = clamp(brightness - u_threshold + u_knee, 0.0, 2.0 * u_knee);
    rq       = (rq * rq) / (4.0 * u_knee + 1.0e-5);
    float w  = max(rq, brightness - u_threshold) / max(brightness, 1.0e-4);

    fragColor = vec4(color * w, 1.0);
}
