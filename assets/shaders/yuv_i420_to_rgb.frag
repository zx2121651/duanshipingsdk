#version 300 es
// yuv_i420_to_rgb.frag
//
// I420 (YUV420 Planar) → RGB 片元着色器。
//
// 输入纹理：
//   texY  — GL_R8，宽 × 高     (Y 亮度平面)
//   texU  — GL_R8，宽/2 × 高/2 (U Cb 色差平面)
//   texV  — GL_R8，宽/2 × 高/2 (V Cr 色差平面)
//
// 色彩空间：BT.601 有限范围 (studio swing)
//   Y: [16, 235]  UV: [16, 240]
//
// 应用场景：
//   FFmpegVideoDecoder — 解码 H.264 / VP8 / VP9 / AV1 软解帧
//   输出格式 YUV420P 或经 swscale 转换后的 I420 帧
//
// 对应 C++ inline 副本：FFmpegVideoDecoder.cpp :: kI420FragSrc

precision highp float;

in vec2 v_uv;
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;
out vec4 fragColor;

void main() {
    float y = texture(texY, v_uv).r - 0.0625;   // offset 16/255
    float u = texture(texU, v_uv).r - 0.5;       // offset 128/255
    float v = texture(texV, v_uv).r - 0.5;

    // BT.601 矩阵
    float r = clamp(1.164 * y + 1.596 * v,                    0.0, 1.0);
    float g = clamp(1.164 * y - 0.391 * u - 0.813 * v,        0.0, 1.0);
    float b = clamp(1.164 * y + 2.018 * u,                    0.0, 1.0);

    fragColor = vec4(r, g, b, 1.0);
}
