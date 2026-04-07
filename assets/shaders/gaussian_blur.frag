#version 300 es
precision mediump float;

uniform sampler2D inputImageTexture;
in vec2 blurCoordinates[5];

out vec4 fragColor;

void main() {
    vec4 sum = vec4(0.0);
    sum += texture(inputImageTexture, blurCoordinates[0]) * 0.204164;
    sum += texture(inputImageTexture, blurCoordinates[1]) * 0.304005;
    sum += texture(inputImageTexture, blurCoordinates[2]) * 0.304005;
    sum += texture(inputImageTexture, blurCoordinates[3]) * 0.093913;
    sum += texture(inputImageTexture, blurCoordinates[4]) * 0.093913;
    fragColor = sum;
}
