#version 300 es
precision mediump float;

in vec2 textureCoordinate;
out vec4 fragColor;

uniform sampler2D inputImageTexture;

uniform float texelWidthOffset;
uniform float texelHeightOffset;
uniform float distanceNormalizationFactor; // Control smoothing strength

void main() {
    vec4 centralColor = texture(inputImageTexture, textureCoordinate);

    // Quick escape for low smoothing (acts as passthrough or just performance saver)
    if (distanceNormalizationFactor < 0.1) {
        fragColor = centralColor;
        return;
    }

    float gaussianWeightTotal = 0.15;
    vec4 sum = centralColor * 0.15;

    vec2 singleStepOffset = vec2(texelWidthOffset, texelHeightOffset);

    float distance_diff;
    vec4 sampleColor;
    float weight;

    // A simplified 3x3 bilateral filter kernel for real-time mobile performance
    // It compares the central pixel color with neighbors. Neighbors with similar colors
    // get higher weights (smoothing continuous areas like skin), while sharp edges
    // are preserved because of high color difference lowering the weight.

    // (-1, -1)
    sampleColor = texture(inputImageTexture, textureCoordinate - singleStepOffset);
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.05 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (0, -1)
    sampleColor = texture(inputImageTexture, textureCoordinate - vec2(0.0, singleStepOffset.y));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.10 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (1, -1)
    sampleColor = texture(inputImageTexture, textureCoordinate + vec2(singleStepOffset.x, -singleStepOffset.y));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.05 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (-1, 0)
    sampleColor = texture(inputImageTexture, textureCoordinate - vec2(singleStepOffset.x, 0.0));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.10 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (1, 0)
    sampleColor = texture(inputImageTexture, textureCoordinate + vec2(singleStepOffset.x, 0.0));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.10 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (-1, 1)
    sampleColor = texture(inputImageTexture, textureCoordinate + vec2(-singleStepOffset.x, singleStepOffset.y));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.05 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (0, 1)
    sampleColor = texture(inputImageTexture, textureCoordinate + vec2(0.0, singleStepOffset.y));
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.10 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    // (1, 1)
    sampleColor = texture(inputImageTexture, textureCoordinate + singleStepOffset);
    distance_diff = distance(centralColor.rgb, sampleColor.rgb);
    weight = 0.05 * exp(-distance_diff * distance_diff * distanceNormalizationFactor);
    sum += sampleColor * weight;
    gaussianWeightTotal += weight;

    fragColor = vec4(sum.rgb / gaussianWeightTotal, centralColor.a);
}