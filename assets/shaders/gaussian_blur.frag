#version 300 es
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;
uniform sampler2D inputImageTexture;
uniform float texelWidthOffset;
uniform float texelHeightOffset;
uniform float blurSize;

void main() {
    vec2 singleStepOffset = vec2(texelWidthOffset, texelHeightOffset);
    vec4 sum = vec4(0.0);

    sum += texture(inputImageTexture, textureCoordinate - singleStepOffset * 4.0) * 0.05;
    sum += texture(inputImageTexture, textureCoordinate - singleStepOffset * 3.0) * 0.09;
    sum += texture(inputImageTexture, textureCoordinate - singleStepOffset * 2.0) * 0.12;
    sum += texture(inputImageTexture, textureCoordinate - singleStepOffset * 1.0) * 0.15;
    sum += texture(inputImageTexture, textureCoordinate) * 0.18;
    sum += texture(inputImageTexture, textureCoordinate + singleStepOffset * 1.0) * 0.15;
    sum += texture(inputImageTexture, textureCoordinate + singleStepOffset * 2.0) * 0.12;
    sum += texture(inputImageTexture, textureCoordinate + singleStepOffset * 3.0) * 0.09;
    sum += texture(inputImageTexture, textureCoordinate + singleStepOffset * 4.0) * 0.05;

    fragColor = sum;
}
