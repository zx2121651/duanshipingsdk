#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float opacity;
out vec4 fragColor;

void main() {
    vec4 bg = texture(texBackground, v_texCoord);
    vec4 fg = texture(texForeground, v_texCoord);

    // Normal alpha blending
    vec3 color = mix(bg.rgb, fg.rgb, fg.a * opacity);
    float alpha = bg.a + fg.a * opacity * (1.0 - bg.a);

    fragColor = vec4(color, alpha);
}
