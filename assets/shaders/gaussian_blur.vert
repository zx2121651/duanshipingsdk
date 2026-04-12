#version 300 es
        layout(location = 0) in vec4 a_position;
        layout(location = 1) in vec4 a_texCoord;

        uniform float texelWidthOffset;
        uniform float texelHeightOffset;
        uniform float blurSize;

        // 传递给片段着色器的 5 个采样点坐标（利用硬件双线性插值，这相当于采样了 9 个点）
        out vec2 blurCoordinates[5];

        void main() {
            gl_Position = a_position;
            vec2 singleStepOffset = vec2(texelWidthOffset, texelHeightOffset) * blurSize;

            // 当前像素点 (中心点)
            blurCoordinates[0] = a_texCoord.xy;
            // 距离中心 1.407333 像素的偏移（经过精心计算的硬件线性插值中心位置）
            blurCoordinates[1] = a_texCoord.xy + singleStepOffset * 1.407333;
            blurCoordinates[2] = a_texCoord.xy - singleStepOffset * 1.407333;
            // 距离中心 3.294215 像素的偏移
            blurCoordinates[3] = a_texCoord.xy + singleStepOffset * 3.294215;
            blurCoordinates[4] = a_texCoord.xy - singleStepOffset * 3.294215;
        }