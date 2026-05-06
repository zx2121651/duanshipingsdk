#version 300 es
// face_reshape.frag — 采样变形后的输入纹理
precision mediump float;
in vec2 v_texCoord;
uniform sampler2D u_inputTexture;
out vec4 fragColor;
void main() {
    fragColor = texture(u_inputTexture, v_texCoord);
}
