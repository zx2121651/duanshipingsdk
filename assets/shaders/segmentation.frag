#version 300 es
// segmentation.frag
//
// 人像分割合成着色器。
//
// 输入：
//   texInput — 原图 RGBA 纹理（全分辨率）
//   texMask  — GL_R8 人像 mask（TFLite 输出，前景=1，背景=0）
//
// 模式：
//   u_mode = 0  BLUR_BG    — 背景由上层 CPU 模糊后传入 texInput2（本 shader 只做 alpha 混合）
//   u_mode = 1  REPLACE_BG — 背景替换为 u_bgColor
//
// 边缘软化：u_edgeSoften 控制 mask 平滑过渡宽度（0=硬边，1=全软化）

precision highp float;

in vec2 v_texCoord;

uniform sampler2D texInput;   // 原图
uniform sampler2D texMask;    // mask（R 通道：前景概率）
uniform int       u_mode;
uniform vec4      u_bgColor;  // REPLACE_BG 背景颜色（归一化 RGBA）
uniform float     u_edgeSoften; // 边缘软化 [0.0, 1.0]

out vec4 fragColor;

void main() {
    vec4  orig = texture(texInput, v_texCoord);
    float fg   = texture(texMask,  v_texCoord).r;

    // 边缘 smoothstep 软化
    float softRange = u_edgeSoften * 0.15;
    float alpha = smoothstep(0.5 - softRange, 0.5 + softRange, fg);

    if (u_mode == 1) {
        // REPLACE_BG：前景保留原图，背景替换为纯色
        fragColor = mix(u_bgColor, orig, alpha);
    } else {
        // BLUR_BG：前景保留原图，背景由 texInput 本身提供（调用方先模糊）
        // 注意：此模式下调用方应传入模糊后的背景作为 texInput，
        //       再在第二 pass 用原图叠加前景。当前 pass 直接 alpha 混合。
        fragColor = vec4(orig.rgb, orig.a * alpha + (1.0 - alpha) * 0.0);
        fragColor = mix(vec4(orig.rgb * 0.15, orig.a), orig, alpha);
    }
}
