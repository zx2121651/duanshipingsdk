/**
 * FaceMorphFilter.cpp
 *
 * 人脸 mesh 变形实现。
 *
 * 网格：(kGridW+1) × (kGridH+1) 顶点，覆盖全屏 NDC [-1,1]
 * 每顶点：position(x,y) + texCoord(u,v)
 * 变形：在 CPU 端移动顶点 UV（不改变顶点位置），
 *       等价于在原图中采样被拉伸/压缩区域。
 */

#include "../../include/ai/FaceMorphFilter.h"
#include "../../include/GLStateManager.h"
#include "../../include/rhi/IRenderDevice.h"

#define LOG_TAG "FaceMorphFilter"
#include "../../include/Log.h"

#include <cmath>
#include <cstring>
#include <algorithm>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// Shaders — vertex: pass-through; fragment: sample input texture
// ---------------------------------------------------------------------------
static const char* kMorphVertSrc = R"(#version 300 es
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
out vec2 v_texCoord;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord  = a_texCoord;
}
)";

static const char* kMorphFragSrc = R"(#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_inputTex;
out vec4 fragColor;
void main() {
    fragColor = texture(u_inputTex, v_texCoord);
}
)";

// ---------------------------------------------------------------------------
FaceMorphFilter::FaceMorphFilter() {
    m_strengths.fill(0.f);
}

FaceMorphFilter::~FaceMorphFilter() {
    if (m_meshVao) glDeleteVertexArrays(1, &m_meshVao);
    if (m_meshVbo) glDeleteBuffers(1, &m_meshVbo);
    if (m_meshIbo) glDeleteBuffers(1, &m_meshIbo);
}

void FaceMorphFilter::setStrength(Effect e, float v) {
    int idx = static_cast<int>(e);
    if (idx >= 0 && idx < static_cast<int>(Effect::COUNT))
        m_strengths[idx] = std::max(0.f, std::min(1.f, v));
}

float FaceMorphFilter::getStrength(Effect e) const {
    int idx = static_cast<int>(e);
    return (idx >= 0 && idx < static_cast<int>(Effect::COUNT)) ? m_strengths[idx] : 0.f;
}

void FaceMorphFilter::resetAll() { m_strengths.fill(0.f); }

void FaceMorphFilter::updateLandmarks(const FaceResult& face) {
    m_face    = face;
    m_hasFace = face.detected;
}

// ---------------------------------------------------------------------------
Result FaceMorphFilter::initialize() {
    Result r = Filter::initialize();
    if (!r.isOk()) return r;
    cacheUniformLocations();
    buildBaseMesh();
    return Result::ok();
}

void FaceMorphFilter::onProgramRecompiled() { cacheUniformLocations(); }

void FaceMorphFilter::cacheUniformLocations() {
    m_locInputTex = glGetUniformLocation(m_programId, "u_inputTex");
}

// ---------------------------------------------------------------------------
// Build the base mesh grid (no warp) — positions in NDC, uvs in [0,1]
// ---------------------------------------------------------------------------
void FaceMorphFilter::buildBaseMesh() {
    std::vector<float> verts;
    verts.reserve(kVertexCount * 4);
    for (int gy = 0; gy <= kGridH; ++gy) {
        for (int gx = 0; gx <= kGridW; ++gx) {
            float u = (float)gx / kGridW;
            float v = (float)gy / kGridH;
            verts.push_back(u * 2.f - 1.f);   // NDC x
            verts.push_back(1.f - v * 2.f);   // NDC y (flipped)
            verts.push_back(u);                // tex u
            verts.push_back(1.f - v);          // tex v (flipped for GL)
        }
    }

    std::vector<uint16_t> indices;
    indices.reserve(kIndexCount);
    for (int gy = 0; gy < kGridH; ++gy) {
        for (int gx = 0; gx < kGridW; ++gx) {
            uint16_t tl = (uint16_t)(gy * (kGridW+1) + gx);
            uint16_t tr = tl + 1;
            uint16_t bl = tl + (kGridW+1);
            uint16_t br = bl + 1;
            indices.insert(indices.end(), {tl, bl, tr, tr, bl, br});
        }
    }

    glGenVertexArrays(1, &m_meshVao);
    glGenBuffers(1, &m_meshVbo);
    glGenBuffers(1, &m_meshIbo);

    glBindVertexArray(m_meshVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_meshVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_meshIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t),
                 indices.data(), GL_STATIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    // texCoord
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    m_meshReady = true;
    LOGI("FaceMorphFilter: mesh built (%d verts, %d indices)",
         kVertexCount, kIndexCount);
}

// ---------------------------------------------------------------------------
// applyWarp — radial displacement of UV coords near control point
// ---------------------------------------------------------------------------
void FaceMorphFilter::applyWarp(std::vector<float>& verts,
                                 float cpX, float cpY,
                                 float dx,  float dy,
                                 float radius) const
{
    int stride = 4; // x,y,u,v
    for (int i = 0; i < kVertexCount; ++i) {
        float u = verts[i*stride + 2];
        float v = verts[i*stride + 3];
        float du = u - cpX, dv = v - cpY;
        float dist = std::sqrt(du*du + dv*dv);
        if (dist >= radius) continue;
        float t = 1.f - dist / radius;
        float w = t * t * (3.f - 2.f*t); // smoothstep falloff
        verts[i*stride + 2] -= dx * w;
        verts[i*stride + 3] -= dy * w;
    }
}

// ---------------------------------------------------------------------------
// updateMesh — apply all active warps from face landmarks
// ---------------------------------------------------------------------------
void FaceMorphFilter::updateMesh() {
    if (!m_meshReady) return;

    // Rebuild base mesh vertices
    std::vector<float> verts;
    verts.reserve(kVertexCount * 4);
    for (int gy = 0; gy <= kGridH; ++gy) {
        for (int gx = 0; gx <= kGridW; ++gx) {
            float u = (float)gx / kGridW;
            float v = (float)gy / kGridH;
            verts.push_back(u * 2.f - 1.f);
            verts.push_back(1.f - v * 2.f);
            verts.push_back(u);
            verts.push_back(1.f - v);
        }
    }

    if (m_hasFace && !m_face.landmarks.empty()) {
        const auto& lm = m_face.landmarks;
        auto lx = [&](int i) { return lm[i].x; };
        auto ly = [&](int i) { return lm[i].y; };

        // SLIM_FACE — cheekbone inward (idx 1,15 = outer cheek)
        float sF = m_strengths[0];
        if (sF > 0.f && lm.size() > 15) {
            float cx = lx(1),  cy = ly(1),  noseX = lx(30);
            float cx2= lx(15), cy2= ly(15);
            float dxL = (noseX - cx)  * sF * 0.12f;
            float dxR = (noseX - cx2) * sF * 0.12f;
            applyWarp(verts, cx,  cy,  -dxL, 0.f, 0.18f);
            applyWarp(verts, cx2, cy2, -dxR, 0.f, 0.18f);
        }

        // BIG_EYES — expand eye area (idx 37,41 left eye; 44,46 right eye)
        float sE = m_strengths[1];
        if (sE > 0.f && lm.size() > 46) {
            float eyeLx = (lx(37)+lx(41))*0.5f, eyeLy = (ly(37)+ly(41))*0.5f;
            float eyeRx = (lx(44)+lx(46))*0.5f, eyeRy = (ly(44)+ly(46))*0.5f;
            float r = 0.10f;
            applyWarp(verts, eyeLx, eyeLy, 0.f,  sE*0.025f, r); // push top up
            applyWarp(verts, eyeLx, eyeLy, 0.f, -sE*0.025f, r); // expand
            applyWarp(verts, eyeRx, eyeRy, 0.f,  sE*0.025f, r);
            applyWarp(verts, eyeRx, eyeRy, 0.f, -sE*0.025f, r);
        }

        // SLIM_JAW — narrow jaw (idx 7,9 jaw sides)
        float sJ = m_strengths[2];
        if (sJ > 0.f && lm.size() > 9) {
            float jLx=lx(7), jLy=ly(7), jRx=lx(9), jRy=ly(9);
            float noseX2 = lx(30);
            float dxJ = (noseX2 - jLx) * sJ * 0.1f;
            applyWarp(verts, jLx, jLy, -dxJ, 0.f, 0.12f);
            dxJ = (noseX2 - jRx) * sJ * 0.1f;
            applyWarp(verts, jRx, jRy, -dxJ, 0.f, 0.12f);
        }

        // NOSE_SLIM — narrow nose wings (idx 83,84)
        float sN = m_strengths[4];
        if (sN > 0.f && lm.size() > 84) {
            float nLx=lx(83), nLy=ly(83), nRx=lx(84), nRy=ly(84);
            float nMid = (nLx+nRx)*0.5f;
            float dxN = (nMid-nLx)*sN*0.35f;
            applyWarp(verts, nLx, nLy, -dxN, 0.f, 0.07f);
            applyWarp(verts, nRx, nRy,  dxN, 0.f, 0.07f);
        }

        // CHIN_SHAPE — elongate/shorten chin (idx 8)
        float sC = m_strengths[7];
        if (sC > 0.f && lm.size() > 8) {
            float chinX=lx(8), chinY=ly(8);
            applyWarp(verts, chinX, chinY, 0.f, sC*0.04f, 0.10f);
        }
    }

    // Upload updated mesh
    glBindBuffer(GL_ARRAY_BUFFER, m_meshVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---------------------------------------------------------------------------
ResultPayload<Texture> FaceMorphFilter::processFrame(
    const Texture& inputTexture, FrameBufferPtr outputFb)
{
    if (m_programId == 0)
        return ResultPayload<Texture>::error(ERR_RENDER_INVALID_STATE,
            "FaceMorphFilter: not initialized");
    if (!outputFb)
        return ResultPayload<Texture>::error(ERR_RENDER_INVALID_STATE,
            "FaceMorphFilter: null outputFb");
    updateMesh();
    onDraw(inputTexture, outputFb);
    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

void FaceMorphFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(m_locInputTex, 0);

    if (m_meshReady) {
        glBindVertexArray(m_meshVao);
        glDrawElements(GL_TRIANGLES, kIndexCount, GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
    } else {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    outputFb->unbind();
}

std::string FaceMorphFilter::getFragmentShaderSource() const { return kMorphFragSrc; }
std::string FaceMorphFilter::getVertexShaderSource()   const { return kMorphVertSrc; }

} // namespace ai
} // namespace video
} // namespace sdk
