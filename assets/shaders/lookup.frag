#version 300 es
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;

uniform sampler2D inputImageTexture;
uniform sampler2D lookupTexture;
uniform float intensity;

void main() {
    vec4 textureColor = texture(inputImageTexture, textureCoordinate);
    // 3D LUT 核心算法：
            // 1. 获取输入像素的 B (蓝) 通道值，将其映射为 0~63 之间的浮点数，这代表着它在 LUT 图的 64 个网格中的位置索引。
            float blueColor = textureColor.b * 63.0;

    // 2. 因为 blueColor 是浮点数，所以它通常落在两个相邻的整数网格之间。
            // 我们算出这两个相邻网格的索引 (quad1 对应向下取整，quad2 对应向上取整)。
            vec2 quad1;
    quad1.y = floor(floor(blueColor) / 8.0);
    quad1.x = floor(blueColor) - (quad1.y * 8.0);

    vec2 quad2;
    quad2.y = floor(ceil(blueColor) / 8.0);
    quad2.x = ceil(blueColor) - (quad2.y * 8.0);

    // 3. 在对应的网格中，根据输入像素的 R (红) 和 G (绿) 算出在这个 8x8 小方块中的精确 x, y 坐标。
            // 0.125 是 1/8 (每个网格占总宽高的 1/8)，0.5/512.0 是半像素偏移，用于防止 OpenGL 边缘采样时出现像素串线。
            vec2 texPos1;
    texPos1.x = (quad1.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);
    texPos1.y = (quad1.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);

    vec2 texPos2;
    texPos2.x = (quad2.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);
    texPos2.y = (quad2.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);

    vec4 newColor1 = texture(lookupTexture, texPos1);
    vec4 newColor2 = texture(lookupTexture, texPos2);

    // 4. 根据蓝通道的小数部分 (fract)，将从两个网格采到的颜色进行 mix (线性插值) 混合。
            // 这消除了色彩渐变时的 Banding (色阶断层) 现象。
            vec4 newColor = mix(newColor1, newColor2, fract(blueColor));
    fragColor = mix(textureColor, vec4(newColor.rgb, textureColor.w), intensity);
}
