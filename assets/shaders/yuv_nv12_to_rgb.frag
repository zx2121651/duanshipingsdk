#version 300 es
// yuv_nv12_to_rgb.frag
//
// NV12 (Y + UV-interleaved) → RGB 片元着色器。
//
// 输入纹理：
//   texY  — GL_R8，  宽 × 高     (Y 亮度平面)
//   texUV — GL_RG8,  宽/2 × 高/2 (UV 交织色差平面, R=U/Cb, G=V/Cr)
//
// 色彩空间：BT.601 有限范围 (studio swing)
//
// 应用场景：
//   FFmpegVideoDecoder — 解码输出为 NV12 格式（部分 H.264 编码器）
//   VideoDecoderAndroid — AMediaCodec 硬解 NV12 输出（参考实现）
//
// 对应 C++ inline 副本：FFmpegVideoDecoder.cpp :: kNV12FragSrc

precision highp float;

in vec2 v_uv;
uniform sampler2D texY;
uniform sampler2D texUV;
out vec4 fragColor;

void main() {
    float y  = texture(texY,  v_uv).r - 0.0625;
    vec2  uv = texture(texUV, v_uv).rg - vec2(0.5, 0.5);  // U=.r, V=.g

    // BT.601 矩阵
    float r = clamp(1.164 * y + 1.596 * uv.y,                    0.0, 1.0);
    float g = clamp(1.164 * y - 0.391 * uv.x - 0.813 * uv.y,    0.0, 1.0);
    float b = clamp(1.164 * y + 2.018 * uv.x,                    0.0, 1.0);

    fragColor = vec4(r, g, b, 1.0);
}
