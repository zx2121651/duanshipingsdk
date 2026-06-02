#version 300 es
// yuv_i420_to_rgb.vert
// 通用全屏顶点着色器，供 I420/NV12 YUV→RGB pass 使用。
// 由 FFmpegVideoDecoder 的 FBO pass 调用（同样以 inline 字符串形式嵌入 .cpp）。

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texCoord;
out vec2 v_uv;

void main() {
    gl_Position = position;
    v_uv = texCoord;
}
