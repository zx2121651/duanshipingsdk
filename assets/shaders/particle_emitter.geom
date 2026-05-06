#version 320 es
// particle_emitter.geom — GLES 3.2 几何着色器
// 将输入的每个点（GL_POINTS）扩展为面向摄像机的四边形（billboard）。
// 顶点着色器传入：粒子中心位置 + 生命期（0-1）→ 几何着色器输出 4 个顶点的 TRIANGLE_STRIP。

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

in  vec4  v_color[];        // 粒子颜色（rgba，alpha = 1-lifetime）
in  float v_size[];         // 粒子半径（NDC 单位）

out vec2  g_uv;
out vec4  g_color;

void main() {
    vec4  center = gl_in[0].gl_Position;
    float half   = v_size[0] * 0.5;

    // 左下
    g_color = v_color[0];
    g_uv    = vec2(0.0, 0.0);
    gl_Position = center + vec4(-half, -half, 0.0, 0.0);
    EmitVertex();

    // 右下
    g_uv    = vec2(1.0, 0.0);
    gl_Position = center + vec4( half, -half, 0.0, 0.0);
    EmitVertex();

    // 左上
    g_uv    = vec2(0.0, 1.0);
    gl_Position = center + vec4(-half,  half, 0.0, 0.0);
    EmitVertex();

    // 右上
    g_uv    = vec2(1.0, 1.0);
    gl_Position = center + vec4( half,  half, 0.0, 0.0);
    EmitVertex();

    EndPrimitive();
}
