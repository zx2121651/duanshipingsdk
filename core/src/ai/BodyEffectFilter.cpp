/**
 * BodyEffectFilter.cpp
 *
 * 身体特效 mesh warp 实现（长腿/瘦身/小头/窄肩/提臀）。
 */

#include "../../include/ai/BodyEffectFilter.h"
#include "../../include/GLStateManager.h"
#include "../../include/rhi/IRenderDevice.h"

#define LOG_TAG "BodyEffectFilter"
#include "../../include/Log.h"

#include <cmath>
#include <algorithm>

// Reuse same shaders as FaceMorphFilter
static const char* kBodyVertSrc = R"(#version 300 es
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
out vec2 v_texCoord;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord  = a_texCoord;
}
)";

static const char* kBodyFragSrc = R"(#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_inputTex;
out vec4 fragColor;
void main() {
    fragColor = texture(u_inputTex, v_texCoord);
}
)";

namespace sdk {
namespace video {
namespace ai {

BodyEffectFilter::BodyEffectFilter() { m_strengths.fill(0.f); }

BodyEffectFilter::~BodyEffectFilter() {
    if (m_meshVao) glDeleteVertexArrays(1, &m_meshVao);
    if (m_meshVbo) glDeleteBuffers(1, &m_meshVbo);
    if (m_meshIbo) glDeleteBuffers(1, &m_meshIbo);
}

void BodyEffectFilter::setStrength(Effect e, float v) {
    int i = static_cast<int>(e);
    if (i>=0 && i<static_cast<int>(Effect::COUNT))
        m_strengths[i] = std::max(0.f, std::min(1.f, v));
}
float BodyEffectFilter::getStrength(Effect e) const {
    int i = static_cast<int>(e);
    return (i>=0 && i<static_cast<int>(Effect::COUNT)) ? m_strengths[i] : 0.f;
}
void BodyEffectFilter::resetAll() { m_strengths.fill(0.f); }
void BodyEffectFilter::updatePose(const BodyPoseResult& p) { m_pose=p; m_hasPose=p.detected; }

Result BodyEffectFilter::initialize() {
    Result r = Filter::initialize();
    if (!r.isOk()) return r;
    cacheUniformLocations();
    buildBaseMesh();
    return Result::ok();
}
void BodyEffectFilter::onProgramRecompiled() { cacheUniformLocations(); }
void BodyEffectFilter::cacheUniformLocations() {
    m_locInputTex = glGetUniformLocation(m_programId, "u_inputTex");
}

void BodyEffectFilter::buildBaseMesh() {
    std::vector<float> verts;
    verts.reserve(kVertexCount * 4);
    for (int gy=0;gy<=kGridH;++gy)
        for (int gx=0;gx<=kGridW;++gx) {
            float u=(float)gx/kGridW, v=(float)gy/kGridH;
            verts.push_back(u*2.f-1.f); verts.push_back(1.f-v*2.f);
            verts.push_back(u);         verts.push_back(1.f-v);
        }
    std::vector<uint16_t> idx;
    idx.reserve(kIndexCount);
    for (int gy=0;gy<kGridH;++gy)
        for (int gx=0;gx<kGridW;++gx) {
            uint16_t tl=(uint16_t)(gy*(kGridW+1)+gx), tr=tl+1,
                     bl=tl+(kGridW+1), br=bl+1;
            idx.insert(idx.end(),{tl,bl,tr,tr,bl,br});
        }
    glGenVertexArrays(1,&m_meshVao); glGenBuffers(1,&m_meshVbo); glGenBuffers(1,&m_meshIbo);
    glBindVertexArray(m_meshVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_meshVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_meshIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(uint16_t), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);
    m_meshReady = true;
}

void BodyEffectFilter::applyWarp(std::vector<float>& verts,
                                  float cpX, float cpY,
                                  float dx, float dy, float radius) const
{
    for (int i=0;i<kVertexCount;++i) {
        float u=verts[i*4+2], v=verts[i*4+3];
        float du=u-cpX, dv=v-cpY, dist=std::sqrt(du*du+dv*dv);
        if (dist>=radius) continue;
        float t=1.f-dist/radius, w=t*t*(3.f-2.f*t);
        verts[i*4+2]-=dx*w; verts[i*4+3]-=dy*w;
    }
}

// Stretch all vertices below belowY by scale factor around belowY
void BodyEffectFilter::applyStretch(std::vector<float>& verts,
                                     float belowY, float scale) const
{
    for (int i=0;i<kVertexCount;++i) {
        float v=verts[i*4+3];
        if (v > belowY) {
            verts[i*4+3] = belowY + (v - belowY) * scale;
        }
    }
}

void BodyEffectFilter::updateMesh() {
    if (!m_meshReady) return;
    std::vector<float> verts;
    verts.reserve(kVertexCount*4);
    for (int gy=0;gy<=kGridH;++gy)
        for (int gx=0;gx<=kGridW;++gx) {
            float u=(float)gx/kGridW, v=(float)gy/kGridH;
            verts.push_back(u*2.f-1.f); verts.push_back(1.f-v*2.f);
            verts.push_back(u);         verts.push_back(1.f-v);
        }

    if (m_hasPose) {
        const auto& kp = m_pose.keypoints;

        // SLIM_BODY (0) — hip inward from both sides
        float sB = m_strengths[0];
        if (sB > 0.f) {
            float hipLx=kp[11].x, hipLy=kp[11].y;
            float hipRx=kp[12].x, hipRy=kp[12].y;
            float midX=(hipLx+hipRx)*0.5f;
            applyWarp(verts, hipLx, hipLy, (hipLx-midX)*sB*0.18f, 0.f, 0.20f);
            applyWarp(verts, hipRx, hipRy, (hipRx-midX)*sB*0.18f, 0.f, 0.20f);
        }

        // LONG_LEGS (1) — stretch below knee
        float sL = m_strengths[1];
        if (sL > 0.f) {
            float kneeY = (kp[13].y + kp[14].y) * 0.5f;
            float legScale = 1.f + sL * 0.18f;
            applyStretch(verts, kneeY, legScale);
        }

        // SMALL_HEAD (2) — compress head region
        float sH = m_strengths[2];
        if (sH > 0.f) {
            float headX=kp[0].x, headY=kp[0].y;
            float scale = 1.f - sH * 0.2f;
            for (int i=0;i<kVertexCount;++i) {
                float u=verts[i*4+2], v=verts[i*4+3];
                float d=std::sqrt((u-headX)*(u-headX)+(v-headY)*(v-headY));
                if (d < 0.22f) {
                    float t=1.f-d/0.22f, w=t*t*(3.f-2.f*t);
                    verts[i*4+2] = headX + (u-headX)*(1.f - (1.f-scale)*w);
                    verts[i*4+3] = headY + (v-headY)*(1.f - (1.f-scale)*w);
                }
            }
        }

        // SLIM_SHOULDER (3) — shoulder inward
        float sSh = m_strengths[3];
        if (sSh > 0.f) {
            float shLx=kp[5].x, shLy=kp[5].y;
            float shRx=kp[6].x, shRy=kp[6].y;
            float midX2=(shLx+shRx)*0.5f;
            applyWarp(verts, shLx, shLy, (shLx-midX2)*sSh*0.14f, 0.f, 0.15f);
            applyWarp(verts, shRx, shRy, (shRx-midX2)*sSh*0.14f, 0.f, 0.15f);
        }

        // LIFT_BUTTOCKS (4) — move hip region up
        float sLift = m_strengths[4];
        if (sLift > 0.f) {
            float buttY=(kp[11].y+kp[12].y)*0.5f;
            float buttX=(kp[11].x+kp[12].x)*0.5f;
            applyWarp(verts, buttX, buttY, 0.f, sLift*0.05f, 0.16f);
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_meshVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size()*sizeof(float), verts.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

ResultPayload<Texture> BodyEffectFilter::processFrame(
    const Texture& inputTexture, FrameBufferPtr outputFb)
{
    if (m_programId==0)
        return ResultPayload<Texture>::error(ERR_RENDER_INVALID_STATE,"BodyEffectFilter: not initialized");
    if (!outputFb)
        return ResultPayload<Texture>::error(ERR_RENDER_INVALID_STATE,"BodyEffectFilter: null outputFb");
    updateMesh();
    onDraw(inputTexture, outputFb);
    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

void BodyEffectFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
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

std::string BodyEffectFilter::getFragmentShaderSource() const { return kBodyFragSrc; }
std::string BodyEffectFilter::getVertexShaderSource()   const { return kBodyVertSrc; }

} // namespace ai
} // namespace video
} // namespace sdk
