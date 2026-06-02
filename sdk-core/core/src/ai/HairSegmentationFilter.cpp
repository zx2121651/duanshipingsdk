/**
 * HairSegmentationFilter.cpp — 发色染色滤镜实现
 */
#include "../../include/ai/HairSegmentationFilter.h"
#include "../../include/GLStateManager.h"
#define LOG_TAG "HairSegmentationFilter"
#include "../../include/Log.h"

namespace sdk {
namespace video {

static const char* kHairFrag = R"(#version 300 es
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_inputTexture;
uniform sampler2D u_hairMask;
uniform vec3  u_hairColor;
uniform float u_colorIntensity;
uniform float u_glossIntensity;
out vec4 fragColor;
vec3 rgb2hsl(vec3 c){
    float mx=max(c.r,max(c.g,c.b)),mn=min(c.r,min(c.g,c.b)),d=mx-mn;
    float l=(mx+mn)*0.5,s=(d<0.001)?0.0:d/(1.0-abs(2.0*l-1.0));
    float h=0.0;
    if(d>0.001){
        if(mx==c.r)h=mod((c.g-c.b)/d,6.0);
        else if(mx==c.g)h=(c.b-c.r)/d+2.0;
        else h=(c.r-c.g)/d+4.0; h/=6.0;
    }
    return vec3(h,s,l);
}
vec3 hsl2rgb(vec3 h){
    float c=(1.0-abs(2.0*h.z-1.0))*h.y,x=c*(1.0-abs(mod(h.x*6.0,2.0)-1.0)),m=h.z-c*0.5;
    int hi=int(h.x*6.0);
    vec3 r;
    if(hi==0)r=vec3(c,x,0.0);else if(hi==1)r=vec3(x,c,0.0);else if(hi==2)r=vec3(0.0,c,x);
    else if(hi==3)r=vec3(0.0,x,c);else if(hi==4)r=vec3(x,0.0,c);else r=vec3(c,0.0,x);
    return r+vec3(m);
}
void main(){
    vec4 orig=texture(u_inputTexture,v_texCoord);
    float mask=smoothstep(0.35,0.65,texture(u_hairMask,v_texCoord).r);
    vec3 oHSL=rgb2hsl(orig.rgb);
    vec3 tHSL=rgb2hsl(u_hairColor);
    vec3 newHSL=vec3(tHSL.x,mix(oHSL.y,max(oHSL.y,tHSL.y),0.6),oHSL.z);
    vec3 recolor=hsl2rgb(newHSL);
    float gloss=smoothstep(0.65,0.9,oHSL.z)*u_glossIntensity*mask;
    recolor=min(recolor+vec3(gloss*0.3),vec3(1.0));
    fragColor=vec4(mix(orig.rgb,recolor,mask*u_colorIntensity),orig.a);
}
)";

HairSegmentationFilter::HairSegmentationFilter() {}

HairSegmentationFilter::~HairSegmentationFilter() {
    if (m_maskTexId) { glDeleteTextures(1, &m_maskTexId); m_maskTexId = 0; }
}

bool HairSegmentationFilter::loadModel(const std::string& p) { return m_engine.loadModel(p); }
bool HairSegmentationFilter::loadModelFromBuffer(const void* d, size_t s) {
    return m_engine.loadModelFromBuffer(d, s);
}

Result HairSegmentationFilter::initialize() {
    Result res = Filter::initialize();
    if (!res.isOk()) return res;
    cacheUniforms();
    return Result::ok();
}

void HairSegmentationFilter::onProgramRecompiled() { cacheUniforms(); }

void HairSegmentationFilter::cacheUniforms() {
    if (!m_programId) return;
    m_uInput          = glGetUniformLocation(m_programId, "u_inputTexture");
    m_uMask           = glGetUniformLocation(m_programId, "u_hairMask");
    m_uHairColor      = glGetUniformLocation(m_programId, "u_hairColor");
    m_uColorIntensity = glGetUniformLocation(m_programId, "u_colorIntensity");
    m_uGloss          = glGetUniformLocation(m_programId, "u_glossIntensity");
}

void HairSegmentationFilter::setHairColor(float r, float g, float b) {
    m_hairR = r; m_hairG = g; m_hairB = b;
}

ResultPayload<Texture> HairSegmentationFilter::processFrame(
    const Texture& inputTexture, FrameBufferPtr outputFb)
{
    int texW = inputTexture.width  > 0 ? inputTexture.width  : 256;
    int texH = inputTexture.height > 0 ? inputTexture.height : 256;

    auto result = m_engine.runInference(inputTexture.id, texW, texH);
    if (result.success) {
        m_maskTexId = result.maskTextureId;
        m_maskW     = result.maskWidth;
        m_maskH     = result.maskHeight;
    }

    return Filter::processFrame(inputTexture, outputFb);
}

void HairSegmentationFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    GLStateManager::getInstance().useProgram(m_programId);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    if (m_uInput >= 0) glUniform1i(m_uInput, 0);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_maskTexId > 0 ? m_maskTexId : 0);
    if (m_uMask >= 0) glUniform1i(m_uMask, 1);

    if (m_uHairColor      >= 0) glUniform3f(m_uHairColor, m_hairR, m_hairG, m_hairB);
    if (m_uColorIntensity >= 0) glUniform1f(m_uColorIntensity, m_colorIntensity);
    if (m_uGloss          >= 0) glUniform1f(m_uGloss, m_glossIntensity);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    outputFb->unbind();
}

std::string HairSegmentationFilter::getFragmentShaderSource() const { return kHairFrag; }

} // namespace video
} // namespace sdk
