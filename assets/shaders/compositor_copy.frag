#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D texForeground;
uniform float opacity;
out vec4 fragColor;

void main() {
    vec4 fg = texture(texForeground, v_texCoord);
    fragColor = vec4(fg.rgb, fg.a * opacity);
}
