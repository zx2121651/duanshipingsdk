#version 300 es
precision highp float;
in vec2 textureCoordinate;
uniform sampler2D inputImageTexture;
uniform sampler2D lookupTexture;
uniform float intensity;

out vec4 fragColor;

void main() {
    vec4 textureColor = texture(inputImageTexture, textureCoordinate);

    // 计算 3D LUT (Look-Up Table) 的坐标映射
    float blueColor = textureColor.b * 63.0;

    vec2 quad1;
    quad1.y = floor(floor(blueColor) / 8.0);
    quad1.x = floor(blueColor) - (quad1.y * 8.0);

    vec2 quad2;
    quad2.y = floor(ceil(blueColor) / 8.0);
    quad2.x = ceil(blueColor) - (quad2.y * 8.0);

    vec2 texPos1;
    texPos1.x = (quad1.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);
    texPos1.y = (quad1.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);

    vec2 texPos2;
    texPos2.x = (quad2.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);
    texPos2.y = (quad2.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);

    vec4 newColor1 = texture(lookupTexture, texPos1);
    vec4 newColor2 = texture(lookupTexture, texPos2);

    vec4 newColor = mix(newColor1, newColor2, fract(blueColor));

    // 增加一定的胶片颗粒噪点感 (Film Grain) 或者 Vignette (暗角)，作为"电影感"的体现
    float dist = distance(textureCoordinate, vec2(0.5, 0.5));
    newColor.rgb *= smoothstep(0.8, 0.3, dist); // Vignette

    fragColor = mix(textureColor, vec4(newColor.rgb, textureColor.w), intensity);
}
