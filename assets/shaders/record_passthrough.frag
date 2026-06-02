#version 300 es
precision mediump float;
in vec2 vtc;
out vec4 c;
uniform sampler2D tex;
void main() {
    c = texture(tex, vtc);
}
