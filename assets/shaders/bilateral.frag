#version 300 es
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;
uniform sampler2D inputImageTexture;
uniform float texelWidthOffset;
uniform float texelHeightOffset;
uniform float distanceNormalizationFactor;

void main() {
    vec4 centralColor = texture(inputImageTexture, textureCoordinate);
    float gaussianWeightTotal = 0.18;
    vec4 sum = centralColor * 0.18;

    vec2 step = vec2(texelWidthOffset, texelHeightOffset);

    // Simple 5-tap bilateral filter approximation
    vec2 offsets[4];
    offsets[0] = vec2(-step.x, -step.y);
    offsets[1] = vec2(step.x, -step.y);
    offsets[2] = vec2(-step.x, step.y);
    offsets[3] = vec2(step.x, step.y);

    for (int i = 0; i < 4; i++) {
        vec4 sampleColor = texture(inputImageTexture, textureCoordinate + offsets[i]);
        float distanceFromCentralColor = distance(centralColor, sampleColor) * distanceNormalizationFactor;
        float weight = 0.15 * (1.0 - min(distanceFromCentralColor, 1.0));
        sum += sampleColor * weight;
        gaussianWeightTotal += weight;
    }

    fragColor = sum / gaussianWeightTotal;
}
