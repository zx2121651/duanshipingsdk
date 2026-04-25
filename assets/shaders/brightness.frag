#version 300 es
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;
uniform sampler2D inputImageTexture;
layout(std140) uniform FilterParams {
    float brightness;
};

void main() {
    vec4 textureColor = texture(inputImageTexture, textureCoordinate);
    fragColor = vec4((textureColor.rgb + vec3(brightness)), textureColor.w);
}