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
    vec4 blended = fg * fg.a * opacity + bg * (1.0 - fg.a * opacity);
    fragColor = vec4(blended.rgb, max(bg.a, fg.a * opacity));
}
