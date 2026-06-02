#version 300 es
// face_reshape.vert — GPU Mesh Warp 人脸重塑顶点着色器
// 对标抖音大眼/瘦脸/瘦鼻/V脸效果
precision highp float;

in vec2 a_position;   // [-1,1] NDC
in vec2 a_texCoord;   // [0,1]

uniform vec2  u_landmarks[106];  // 归一化关键点坐标 [0,1]
uniform bool  u_hasFace;
uniform float u_eyeScale;    // 大眼强度 [0,1]
uniform float u_faceSlim;    // 瘦脸强度 [0,1]
uniform float u_noseSlim;    // 瘦鼻强度 [0,1]
uniform float u_foreheadUp;  // 额头 [0,1]
uniform float u_chinV;       // V脸/下颌 [0,1]
uniform float u_mouthWidth;  // 嘴型 [-1,1]

out vec2 v_texCoord;

// 平滑衰减函数：距控制点 center 越近影响越强
float falloff(vec2 pos, vec2 center, float radius) {
    float d = length(pos - center);
    return clamp(1.0 - d / radius, 0.0, 1.0);
    // 使用 smoothstep 使边缘过渡更自然
    // return smoothstep(radius, 0.0, d);
}

// 对单个控制点施加位移
vec2 warpPoint(vec2 uv, vec2 controlPt, vec2 delta, float radius) {
    float w = falloff(uv, controlPt, radius);
    return uv + delta * w * w; // 平方使边缘更柔
}

void main() {
    v_texCoord = a_texCoord;
    vec2 uv = a_texCoord; // 在纹理空间做变形，再转回 NDC

    if (u_hasFace) {
        // ── 大眼（右眼中心 = landmarks[39]，左眼 = landmarks[42]）
        if (u_eyeScale > 0.001) {
            float eyeR = 0.06;
            vec2 reye = u_landmarks[39];
            vec2 leye = u_landmarks[42];
            float ew = u_eyeScale * 0.04;
            // 向外扩张：远离眼球中心
            uv = warpPoint(uv, reye, normalize(uv - reye) * ew, eyeR);
            uv = warpPoint(uv, leye, normalize(uv - leye) * ew, eyeR);
        }

        // ── 瘦脸（颧骨 = landmarks[1]/landmarks[15]，向内收）
        if (u_faceSlim > 0.001) {
            float faceR = 0.20;
            vec2 rcheek = u_landmarks[1];
            vec2 lcheek = u_landmarks[15];
            vec2 chin   = u_landmarks[8];
            vec2 fw  = vec2(u_faceSlim * 0.06, 0.0);
            // 左颧骨向右收，右颧骨向左收
            uv = warpPoint(uv, rcheek, vec2( fw.x, 0.0), faceR);
            uv = warpPoint(uv, lcheek, vec2(-fw.x, 0.0), faceR);
            // 下颌收窄
            float cw = u_faceSlim * 0.03;
            uv = warpPoint(uv, chin, vec2(0.0, cw), 0.12);
        }

        // ── V脸（下颌点 = landmarks[8]，向上提）
        if (u_chinV > 0.001) {
            vec2 chin = u_landmarks[8];
            float cy  = u_chinV * 0.04;
            uv = warpPoint(uv, chin, vec2(0.0, -cy), 0.14);
            // 两侧下颌也向中心收
            vec2 cl = u_landmarks[5];
            vec2 cr = u_landmarks[11];
            float cx2 = u_chinV * 0.025;
            uv = warpPoint(uv, cl, vec2( cx2, -cx2), 0.10);
            uv = warpPoint(uv, cr, vec2(-cx2, -cx2), 0.10);
        }

        // ── 瘦鼻（鼻翼 = landmarks[31]/landmarks[35]，向鼻梁收）
        if (u_noseSlim > 0.001) {
            vec2 noseL = u_landmarks[31];
            vec2 noseR = u_landmarks[35];
            vec2 noseBridge = u_landmarks[27];
            float nx = u_noseSlim * 0.025;
            uv = warpPoint(uv, noseL, vec2( nx, 0.0), 0.06);
            uv = warpPoint(uv, noseR, vec2(-nx, 0.0), 0.06);
        }

        // ── 额头（向上提，landmarks[27] = 鼻根，当做额头下边界）
        if (u_foreheadUp > 0.001) {
            vec2 fore = u_landmarks[27];
            fore.y -= 0.12; // 额头中心估算
            float fy = u_foreheadUp * 0.04;
            uv = warpPoint(uv, fore, vec2(0.0, -fy), 0.18);
        }
    }

    // 防止越界
    uv = clamp(uv, vec2(0.0), vec2(1.0));
    v_texCoord = uv;

    gl_Position = vec4(a_position, 0.0, 1.0);
}
