#version 320 es
// msaa_resolve.frag — GLES 3.2 MSAA 多采样纹理 resolve
// 将 sampler2DMS（多采样纹理）通过均值降采样写入单采样输出。
// 用法：绑定 GL_TEXTURE_2D_MULTISAMPLE → 绘制全屏四边形 → 写入普通 FBO。

precision highp float;

in vec2 v_texCoord;

uniform highp sampler2DMS texMSAA;  // GL_TEXTURE_2D_MULTISAMPLE
uniform int               u_samples; // MSAA 采样数（2 / 4 / 8）

out vec4 fragColor;

void main() {
    // 将归一化 UV 转换回整数像素坐标（sampler2DMS 需要 ivec2）
    ivec2 texSize  = textureSize(texMSAA);
    ivec2 texel    = ivec2(v_texCoord * vec2(texSize));

    // 累积所有采样点
    vec4 accum = vec4(0.0);
    for (int i = 0; i < u_samples; ++i) {
        accum += texelFetch(texMSAA, texel, i);
    }
    fragColor = accum / float(u_samples);
}
