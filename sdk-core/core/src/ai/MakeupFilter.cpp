/**
 * MakeupFilter.cpp — 多层 AI 美妆滤镜实现
 */
#include "../../include/ai/MakeupFilter.h"
#include "../../include/GLStateManager.h"
#define LOG_TAG "MakeupFilter"
#include "../../include/Log.h"

#include <algorithm>

namespace sdk {
namespace video {

MakeupFilter::MakeupFilter() {
    // 默认口红：玫瑰红，强度 0（关闭）
    m_lip      = {0.85f, 0.18f, 0.25f, 0.f};
    m_blush    = {0.95f, 0.55f, 0.55f, 0.f};
    m_eyeshadow= {0.35f, 0.20f, 0.50f, 0.f};
    m_highlight= {1.0f,  1.0f,  1.0f,  0.f};
    m_contour  = {0.55f, 0.38f, 0.28f, 0.f};
    m_eyebrow  = {0.25f, 0.18f, 0.12f, 0.f};
}

Result MakeupFilter::initialize() {
    Result res = Filter::initialize();
    if (!res.isOk()) return res;
    cacheUniforms();
    return Result::ok();
}

void MakeupFilter::onProgramRecompiled() { cacheUniforms(); }

void MakeupFilter::cacheUniforms() {
    if (!m_programId) return;
    m_uTexture        = glGetUniformLocation(m_programId, "u_inputTexture");
    m_uHasFace        = glGetUniformLocation(m_programId, "u_hasFace");
    m_uLandmarks      = glGetUniformLocation(m_programId, "u_landmarks");
    m_uLipColor       = glGetUniformLocation(m_programId, "u_lipColor");
    m_uLipIntensity   = glGetUniformLocation(m_programId, "u_lipIntensity");
    m_uBlushColor     = glGetUniformLocation(m_programId, "u_blushColor");
    m_uBlushIntensity = glGetUniformLocation(m_programId, "u_blushIntensity");
    m_uEyeColor       = glGetUniformLocation(m_programId, "u_eyeColor");
    m_uEyeIntensity   = glGetUniformLocation(m_programId, "u_eyeIntensity");
    m_uHighlight      = glGetUniformLocation(m_programId, "u_highlight");
    m_uContour        = glGetUniformLocation(m_programId, "u_contour");
    m_uEyebrowColor   = glGetUniformLocation(m_programId, "u_eyebrowColor");
    m_uEyebrowIntens  = glGetUniformLocation(m_programId, "u_eyebrowIntensity");
}

void MakeupFilter::setLandmarkResult(const ai::LandmarkFrameResult& r) {
    m_hasFace = (r.faceCount > 0 && r.faces[0].detected);
    m_faceResult = m_hasFace ? r.faces[0] : ai::FaceResult{};
}

void MakeupFilter::setLipColor  (float r,float g,float b,float i){ m_lip      ={r,g,b,i}; }
void MakeupFilter::setBlush     (float r,float g,float b,float i){ m_blush    ={r,g,b,i}; }
void MakeupFilter::setEyeshadow (float r,float g,float b,float i){ m_eyeshadow={r,g,b,i}; }
void MakeupFilter::setHighlight (float i)                         { m_highlight.intensity=i; }
void MakeupFilter::setContour   (float i)                         { m_contour.intensity=i;  }
void MakeupFilter::setEyebrow   (float r,float g,float b,float i){ m_eyebrow  ={r,g,b,i}; }

void MakeupFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    auto outputPass = beginOutputRenderPass(outputFb);
    GLStateManager::getInstance().useProgram(m_programId);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    if (m_uTexture    >= 0) glUniform1i(m_uTexture, 0);
    if (m_uHasFace    >= 0) glUniform1i(m_uHasFace, m_hasFace ? 1 : 0);

    if (m_uLandmarks >= 0 && m_hasFace) {
        float pts[ai::kFaceLandmarkCount * 2];
        for (int i = 0; i < ai::kFaceLandmarkCount; ++i) {
            pts[i*2+0] = std::clamp(m_faceResult.landmarks[i].x, 0.0f, 1.0f);
            pts[i*2+1] = std::clamp(m_faceResult.landmarks[i].y, 0.0f, 1.0f);
        }
        glUniform2fv(m_uLandmarks, ai::kFaceLandmarkCount, pts);
    }

    if (m_uLipColor      >= 0) glUniform3f(m_uLipColor,      m_lip.r,       m_lip.g,       m_lip.b);
    if (m_uLipIntensity  >= 0) glUniform1f(m_uLipIntensity,  m_lip.intensity);
    if (m_uBlushColor    >= 0) glUniform3f(m_uBlushColor,    m_blush.r,     m_blush.g,     m_blush.b);
    if (m_uBlushIntensity>= 0) glUniform1f(m_uBlushIntensity,m_blush.intensity);
    if (m_uEyeColor      >= 0) glUniform3f(m_uEyeColor,      m_eyeshadow.r, m_eyeshadow.g, m_eyeshadow.b);
    if (m_uEyeIntensity  >= 0) glUniform1f(m_uEyeIntensity,  m_eyeshadow.intensity);
    if (m_uHighlight     >= 0) glUniform1f(m_uHighlight,     m_highlight.intensity);
    if (m_uContour       >= 0) glUniform1f(m_uContour,       m_contour.intensity);
    if (m_uEyebrowColor  >= 0) glUniform3f(m_uEyebrowColor,  m_eyebrow.r,   m_eyebrow.g,   m_eyebrow.b);
    if (m_uEyebrowIntens >= 0) glUniform1f(m_uEyebrowIntens, m_eyebrow.intensity);

    if (m_renderDevice && m_quadVao) {
        auto cmd = outputPass.commandBuffer ? outputPass.commandBuffer
                                            : m_renderDevice->createCommandBuffer();
        cmd->bindVertexArray(m_quadVao.get());
        cmd->draw(4);
    } else {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    endOutputRenderPass(outputPass, outputFb);
}

static const char* kMakeupFrag = R"(#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_inputTexture;
uniform bool  u_hasFace;
uniform vec2  u_landmarks[106];
uniform vec3  u_lipColor;       uniform float u_lipIntensity;
uniform vec3  u_blushColor;     uniform float u_blushIntensity;
uniform vec3  u_eyeColor;       uniform float u_eyeIntensity;
uniform float u_highlight;
uniform float u_contour;
uniform vec3  u_eyebrowColor;   uniform float u_eyebrowIntensity;
out vec4 fragColor;
float gmask(vec2 uv,vec2 c,float r){float d=length(uv-c);return exp(-d*d/(2.0*r*r));}
vec3 overlay(vec3 b,vec3 s){return mix(2.0*b*s,1.0-2.0*(1.0-b)*(1.0-s),step(0.5,b));}
vec3 screen(vec3 b,vec3 s){return 1.0-(1.0-b)*(1.0-s);}
vec3 multiply(vec3 b,vec3 s){return b*s;}
void main(){
    vec4 orig=texture(u_inputTexture,v_texCoord);
    vec3 c=orig.rgb;
    if(!u_hasFace){fragColor=orig;return;}
    if(u_lipIntensity>0.001){
        vec2 lc=vec2(0.0);for(int i=48;i<68;++i)lc+=u_landmarks[i];lc/=20.0;
        float mw=max(length(u_landmarks[54]-u_landmarks[48])*0.5,0.04);
        float mh=max(length(u_landmarks[57]-u_landmarks[51])*0.6,0.025);
        vec2 dd=(v_texCoord-lc)/vec2(mw,mh);
        float lm=smoothstep(1.0,0.3,dot(dd,dd));
        c=mix(c,overlay(c,u_lipColor),lm*u_lipIntensity);
    }
    if(u_blushIntensity>0.001){
        float bm=clamp(gmask(v_texCoord,u_landmarks[1],0.08)+gmask(v_texCoord,u_landmarks[15],0.08),0.0,1.0)*0.85;
        c=mix(c,screen(c,u_blushColor),bm*u_blushIntensity);
    }
    if(u_eyeIntensity>0.001){
        vec2 er=(u_landmarks[37]+u_landmarks[38])*0.5;
        vec2 el=(u_landmarks[43]+u_landmarks[44])*0.5;
        float em=clamp(gmask(v_texCoord,er,0.05)+gmask(v_texCoord,el,0.05),0.0,1.0)*0.9;
        c=mix(c,multiply(c,mix(vec3(1.0),u_eyeColor,0.8)),em*u_eyeIntensity);
    }
    if(u_highlight>0.001){
        vec2 h0=u_landmarks[27]+vec2(0.0,-0.06);
        vec2 h1=u_landmarks[30];
        float hm=clamp(gmask(v_texCoord,h0,0.035)+gmask(v_texCoord,h1,0.03),0.0,1.0);
        c=mix(c,min(c+vec3(0.25),vec3(1.0)),hm*u_highlight);
    }
    if(u_contour>0.001){
        float cm=clamp(gmask(v_texCoord,u_landmarks[0]+vec2(-0.02,0.0),0.06)+
                       gmask(v_texCoord,u_landmarks[16]+vec2(0.02,0.0),0.06),0.0,1.0);
        c=mix(c,multiply(c,vec3(0.55,0.38,0.28)),cm*u_contour*0.7);
    }
    if(u_eyebrowIntensity>0.001){
        vec2 br=vec2(0.0);for(int i=17;i<22;++i)br+=u_landmarks[i];br/=5.0;
        vec2 bl=vec2(0.0);for(int i=22;i<27;++i)bl+=u_landmarks[i];bl/=5.0;
        float bm=clamp((gmask(v_texCoord,br,0.025)+gmask(v_texCoord,bl,0.025))*2.0,0.0,1.0);
        c=mix(c,u_eyebrowColor,bm*u_eyebrowIntensity*0.75);
    }
    fragColor=vec4(c,orig.a);
}
)";

std::string MakeupFilter::getFragmentShaderSource() const { return kMakeupFrag; }

} // namespace video
} // namespace sdk
