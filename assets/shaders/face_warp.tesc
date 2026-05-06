#version 320 es
// face_warp.tesc — GLES 3.2 细分控制着色器（Tessellation Control Shader）
// 将人脸网格三角形细分，用于平滑变形（大眼/瘦脸等效果的网格插值）。
// 细分级别由 uniform u_tessLevel 控制（1~8，默认 4）。

layout(vertices = 3) out;

in  vec2 v_uv[];
in  vec3 v_pos[];

out vec2 tc_uv[];
out vec3 tc_pos[];

uniform float u_tessLevel; // [1, 8]，越高越精细，性能开销越大

void main() {
    tc_uv[gl_InvocationID]  = v_uv[gl_InvocationID];
    tc_pos[gl_InvocationID] = v_pos[gl_InvocationID];

    if (gl_InvocationID == 0) {
        float lvl = clamp(u_tessLevel, 1.0, 8.0);
        gl_TessLevelOuter[0] = lvl;
        gl_TessLevelOuter[1] = lvl;
        gl_TessLevelOuter[2] = lvl;
        gl_TessLevelInner[0] = lvl;
    }
}
