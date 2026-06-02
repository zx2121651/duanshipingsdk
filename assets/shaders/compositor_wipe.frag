#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float progress; // 0.0 to 1.0
out vec4 fragColor;

void main() {
    vec4 bg = texture(texBackground, v_texCoord);
    vec4 fg = texture(texForeground, v_texCoord);

    // Wipe left transition
    if (v_texCoord.x > (1.0 - progress)) {
        fragColor = fg;
    } else {
        fragColor = bg;
    }
}
