#version 300 es
precision mediump float;

in vec2 blurCoordinates[5];
out vec4 fragColor;
uniform sampler2D inputImageTexture;

void main() {
    // 采用经过权重优化的 5-tap 高斯采样，由于在 VS 中利用了硬件线性插值偏移，
    // 这 5 个点实际上覆盖了 9 个像素的算术权重，达到了 9-tap 的模糊质量，但只有 5 次 Texture Fetch 开销。
    vec4 sum = vec4(0.0);
    sum += texture(inputImageTexture, blurCoordinates[0]) * 0.204164;
    sum += texture(inputImageTexture, blurCoordinates[1]) * 0.304005;
    sum += texture(inputImageTexture, blurCoordinates[2]) * 0.304005;
    sum += texture(inputImageTexture, blurCoordinates[3]) * 0.093913;
    sum += texture(inputImageTexture, blurCoordinates[4]) * 0.093913;
    fragColor = sum;
}
