/**
 * FaceReshapeFilter.cpp
 *
 * GPU Mesh Warp 人脸重塑滤镜实现。
 * 核心思路：全屏四边形 + 顶点着色器读取关键点位移 → Fragment 采样扭曲UV。
 */

#include "../../include/ai/FaceReshapeFilter.h"
#include "../../include/GLStateManager.h"

#define LOG_TAG "FaceReshapeFilter"
#include "../../include/Log.h"

#include <vector>
#include <cstring>

namespace sdk {
namespace video {

// ---------------------------------------------------------------------------
// 静态 Shader 源码
// ---------------------------------------------------------------------------
static const char* kFaceReshapeVert = R"(#version 300 es
precision highp float;
in vec2 a_position;
in vec2 a_texCoord;
uniform vec2  u_landmarks[106];
uniform bool  u_hasFace;
uniform float u_eyeScale;
uniform float u_faceSlim;
uniform float u_noseSlim;
uniform float u_foreheadUp;
uniform float u_chinV;
uniform float u_mouthWidth;
out vec2 v_texCoord;
float falloff(vec2 pos, vec2 center, float radius) {
    float d = length(pos - center);
    return clamp(1.0 - d / radius, 0.0, 1.0);
}
vec2 warpPoint(vec2 uv, vec2 ctrl, vec2 delta, float r) {
    float w = falloff(uv, ctrl, r);
    return uv + delta * w * w;
}
void main() {
    vec2 uv = a_texCoord;
    if (u_hasFace) {
        if (u_eyeScale > 0.001) {
            float er = 0.06; float ew = u_eyeScale * 0.04;
            vec2 re = u_landmarks[39]; vec2 le = u_landmarks[42];
            uv = warpPoint(uv, re, normalize(uv - re) * ew, er);
            uv = warpPoint(uv, le, normalize(uv - le) * ew, er);
        }
        if (u_faceSlim > 0.001) {
            float fw = u_faceSlim * 0.06;
            uv = warpPoint(uv, u_landmarks[1],  vec2( fw, 0.0), 0.20);
            uv = warpPoint(uv, u_landmarks[15], vec2(-fw, 0.0), 0.20);
            uv = warpPoint(uv, u_landmarks[8],  vec2(0.0, u_faceSlim*0.03), 0.12);
        }
        if (u_chinV > 0.001) {
            float cy = u_chinV * 0.04;
            uv = warpPoint(uv, u_landmarks[8],  vec2(0.0, -cy), 0.14);
            float cx2 = u_chinV * 0.025;
            uv = warpPoint(uv, u_landmarks[5],  vec2( cx2, -cx2), 0.10);
            uv = warpPoint(uv, u_landmarks[11], vec2(-cx2, -cx2), 0.10);
        }
        if (u_noseSlim > 0.001) {
            float nx = u_noseSlim * 0.025;
            uv = warpPoint(uv, u_landmarks[31], vec2( nx, 0.0), 0.06);
            uv = warpPoint(uv, u_landmarks[35], vec2(-nx, 0.0), 0.06);
        }
        if (u_foreheadUp > 0.001) {
            vec2 fore = u_landmarks[27]; fore.y -= 0.12;
            uv = warpPoint(uv, fore, vec2(0.0, -u_foreheadUp*0.04), 0.18);
        }
        if (abs(u_mouthWidth) > 0.001) {
            float mw = u_mouthWidth * 0.03;
            uv = warpPoint(uv, u_landmarks[48], vec2(-mw, 0.0), 0.06);
            uv = warpPoint(uv, u_landmarks[54], vec2( mw, 0.0), 0.06);
        }
    }
    uv = clamp(uv, vec2(0.0), vec2(1.0));
    v_texCoord  = uv;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

static const char* kFaceReshapeFrag = R"(#version 300 es
precision mediump float;
in vec2 v_texCoord;
uniform sampler2D u_inputTexture;
out vec4 fragColor;
void main() {
    fragColor = texture(u_inputTexture, v_texCoord);
}
)";

// ---------------------------------------------------------------------------
FaceReshapeFilter::FaceReshapeFilter() {}

FaceReshapeFilter::~FaceReshapeFilter() {
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
}

Result FaceReshapeFilter::initialize() {
    Result res = Filter::initialize();
    if (!res.isOk()) return res;
    buildMesh();
    cacheUniforms();
    return Result::ok();
}

void FaceReshapeFilter::onProgramRecompiled() { cacheUniforms(); }

// ---------------------------------------------------------------------------
void FaceReshapeFilter::buildMesh() {
    // 构建 kGridW × kGridH 全屏四边形网格
    // 每个顶点：(position.xy, texCoord.xy) = 4 floats
    std::vector<float> verts;
    verts.reserve((kGridW + 1) * (kGridH + 1) * 4);
    for (int gy = 0; gy <= kGridH; ++gy) {
        for (int gx = 0; gx <= kGridW; ++gx) {
            float u = static_cast<float>(gx) / kGridW;
            float v = static_cast<float>(gy) / kGridH;
            verts.push_back(u * 2.f - 1.f); // NDC x
            verts.push_back(v * 2.f - 1.f); // NDC y
            verts.push_back(u);             // tex u
            verts.push_back(v);             // tex v
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(kGridW * kGridH * 6);
    for (int gy = 0; gy < kGridH; ++gy) {
        for (int gx = 0; gx < kGridW; ++gx) {
            uint32_t tl = gy * (kGridW + 1) + gx;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + (kGridW + 1);
            uint32_t br = bl + 1;
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    constexpr int stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void FaceReshapeFilter::cacheUniforms() {
    if (!m_programId) return;
    m_uTexture    = glGetUniformLocation(m_programId, "u_inputTexture");
    m_uLandmarks  = glGetUniformLocation(m_programId, "u_landmarks");
    m_uHasFace    = glGetUniformLocation(m_programId, "u_hasFace");
    m_uEyeScale   = glGetUniformLocation(m_programId, "u_eyeScale");
    m_uFaceSlim   = glGetUniformLocation(m_programId, "u_faceSlim");
    m_uNoseSlim   = glGetUniformLocation(m_programId, "u_noseSlim");
    m_uForeheadUp = glGetUniformLocation(m_programId, "u_foreheadUp");
    m_uChinV      = glGetUniformLocation(m_programId, "u_chinV");
    m_uMouthWidth = glGetUniformLocation(m_programId, "u_mouthWidth");
}

// ---------------------------------------------------------------------------
void FaceReshapeFilter::setLandmarkResult(const ai::LandmarkFrameResult& r) {
    m_hasFace = (r.faceCount > 0 && r.faces[0].detected);
    if (m_hasFace) m_faceResult = r.faces[0];
}

// ---------------------------------------------------------------------------
void FaceReshapeFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    // 纹理
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    if (m_uTexture >= 0) glUniform1i(m_uTexture, 0);

    // 效果参数
    if (m_uHasFace    >= 0) glUniform1i(m_uHasFace,    m_hasFace ? 1 : 0);
    if (m_uEyeScale   >= 0) glUniform1f(m_uEyeScale,   m_eyeScale);
    if (m_uFaceSlim   >= 0) glUniform1f(m_uFaceSlim,   m_faceSlim);
    if (m_uNoseSlim   >= 0) glUniform1f(m_uNoseSlim,   m_noseSlim);
    if (m_uForeheadUp >= 0) glUniform1f(m_uForeheadUp, m_foreheadUp);
    if (m_uChinV      >= 0) glUniform1f(m_uChinV,      m_chinV);
    if (m_uMouthWidth >= 0) glUniform1f(m_uMouthWidth, m_mouthWidth);

    // 关键点数组
    if (m_uLandmarks >= 0 && m_hasFace) {
        float pts[ai::kFaceLandmarkCount * 2];
        for (int i = 0; i < ai::kFaceLandmarkCount; ++i) {
            pts[i * 2 + 0] = m_faceResult.landmarks[i].x;
            pts[i * 2 + 1] = m_faceResult.landmarks[i].y;
        }
        glUniform2fv(m_uLandmarks, ai::kFaceLandmarkCount, pts);
    }

    // 绘制网格
    if (m_vao) {
        glBindVertexArray(m_vao);
        glDrawElements(GL_TRIANGLES,
                       kGridW * kGridH * 6,
                       GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    outputFb->unbind();
}

std::string FaceReshapeFilter::getVertexShaderSource()   const { return kFaceReshapeVert; }
std::string FaceReshapeFilter::getFragmentShaderSource() const { return kFaceReshapeFrag; }

} // namespace video
} // namespace sdk
