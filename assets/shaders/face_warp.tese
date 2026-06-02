#version 320 es
// face_warp.tese — GLES 3.2 细分求值着色器（Tessellation Evaluation Shader）
// 对细分后的顶点应用人脸变形向量（存储在 texWarpField 中的 RG 偏移量）。

layout(triangles, equal_spacing, ccw) in;

in  vec2 tc_uv[];
in  vec3 tc_pos[];

out vec2 te_uv;

uniform sampler2D texWarpField;  // 变形场：RG = uv 偏移，B = mask（0=无变形，1=全变形）
uniform float     u_warpStrength; // [0.0, 1.0]

// 重心坐标插值辅助
vec2 baryInterp2(vec2 a, vec2 b, vec2 c) {
    return gl_TessCoord.x * a + gl_TessCoord.y * b + gl_TessCoord.z * c;
}
vec3 baryInterp3(vec3 a, vec3 b, vec3 c) {
    return gl_TessCoord.x * a + gl_TessCoord.y * b + gl_TessCoord.z * c;
}

void main() {
    vec2 uv  = baryInterp2(tc_uv[0],  tc_uv[1],  tc_uv[2]);
    vec3 pos = baryInterp3(tc_pos[0], tc_pos[1], tc_pos[2]);

    // 从变形场纹理读取偏移量
    vec3 warp  = texture(texWarpField, uv).rgb;
    vec2 delta = (warp.rg - 0.5) * 2.0;  // [-1, 1]
    float mask = warp.b;

    // 应用变形
    pos.xy += delta * mask * u_warpStrength * 0.05;

    te_uv       = uv;
    gl_Position = vec4(pos, 1.0);
}
