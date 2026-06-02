/**
 * SplitScreenCompositor.cpp
 *
 * 分屏布局合成器实现。
 */

#include "../../include/timeline/SplitScreenCompositor.h"
#include "../../include/GLStateManager.h"

#define LOG_TAG "SplitScreenCompositor"
#include "../../include/Log.h"

#include <cstring>
#include <cstdio>

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// Minimal quad shaders for each cell
// ---------------------------------------------------------------------------
static const char* kSplitVertSrc = R"(#version 300 es
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
out vec2 v_uv;
uniform int u_flipH;
uniform int u_flipV;
void main(){
    gl_Position = vec4(a_pos, 0.0, 1.0);
    vec2 uv = a_uv;
    if (u_flipH != 0) uv.x = 1.0 - uv.x;
    if (u_flipV != 0) uv.y = 1.0 - uv.y;
    v_uv = uv;
}
)";

static const char* kSplitFragSrc = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_inputTex;
out vec4 fragColor;
void main(){
    fragColor = texture(u_inputTex, v_uv);
}
)";

// ---------------------------------------------------------------------------
static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

GLuint SplitScreenCompositor::compileProgram(const char* vert, const char* frag) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vert);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

// ---------------------------------------------------------------------------
SplitScreenCompositor::SplitScreenCompositor() {
    setLayout(Layout::SPLIT_H);
}

SplitScreenCompositor::~SplitScreenCompositor() {
    release();
}

bool SplitScreenCompositor::initialize() {
    m_program = compileProgram(kSplitVertSrc, kSplitFragSrc);
    m_locInputTex = glGetUniformLocation(m_program, "u_inputTex");
    m_locFlipH    = glGetUniformLocation(m_program, "u_flipH");
    m_locFlipV    = glGetUniformLocation(m_program, "u_flipV");

    // Single quad VBO/VAO — geometry uploaded per cell in drawCell
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 4 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);

    m_initialized = true;
    LOGI("SplitScreenCompositor: initialized (program=%u)", m_program);
    return true;
}

void SplitScreenCompositor::release() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao=0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo=0; }
    if (m_program) { glDeleteProgram(m_program); m_program=0; }
    m_initialized = false;
}

// ---------------------------------------------------------------------------
void SplitScreenCompositor::setLayout(Layout layout) {
    m_layout = layout;
    int count = (layout == Layout::CUSTOM) ? (int)m_cells.size() :
                (layout == Layout::SINGLE) ? 1 :
                (layout == Layout::GRID_2x2) ? 4 :
                (layout == Layout::GRID_3x2) ? 6 :
                (layout == Layout::GRID_3x3) ? 9 : 2;
    m_cells.resize(count);
    // Will be positioned on first render() call
}

void SplitScreenCompositor::setSlot(int slot, GLuint texId) {
    if (slot >= 0 && slot < (int)m_cells.size())
        m_cells[slot].texId = texId;
}

void SplitScreenCompositor::setCells(const std::vector<Cell>& cells) {
    m_layout = Layout::CUSTOM;
    m_cells  = cells;
}

// ---------------------------------------------------------------------------
void SplitScreenCompositor::buildLayout(Layout layout, int outputW, int outputH) {
    if (layout == Layout::CUSTOM) return;
    float gap = (outputW > 0) ? (float)m_gapPx / outputW : 0.f;
    float gapV= (outputH > 0) ? (float)m_gapPx / outputH : 0.f;

    switch (layout) {
    case Layout::SINGLE:
        m_cells.resize(1);
        m_cells[0] = {0.f, 0.f, 1.f, 1.f, m_cells[0].texId};
        break;
    case Layout::SPLIT_H: {  // Left-right
        float w = (1.f - gap) * 0.5f;
        m_cells.resize(2);
        m_cells[0] = {0.f,    0.f, w, 1.f, m_cells[0].texId};
        m_cells[1] = {w+gap,  0.f, w, 1.f, m_cells[1].texId};
        break;
    }
    case Layout::GRID_2x2: {
        float w=(1.f-gap)*0.5f, h=(1.f-gapV)*0.5f;
        m_cells.resize(4);
        m_cells[0]={0.f,   0.f,   w, h, m_cells[0].texId};
        m_cells[1]={w+gap, 0.f,   w, h, m_cells[1].texId};
        m_cells[2]={0.f,   h+gapV,w, h, m_cells[2].texId};
        m_cells[3]={w+gap, h+gapV,w, h, m_cells[3].texId};
        break;
    }
    case Layout::GRID_3x2: {
        float w=(1.f-2*gap)/3.f, h=(1.f-gapV)*0.5f;
        m_cells.resize(6);
        for (int r=0;r<2;++r)
            for (int c=0;c<3;++c) {
                int i=r*3+c;
                GLuint tid=(i<(int)m_cells.size())?m_cells[i].texId:0;
                m_cells[i]={c*(w+gap), r*(h+gapV), w, h, tid};
            }
        break;
    }
    case Layout::GRID_3x3: {
        float w=(1.f-2*gap)/3.f, h=(1.f-2*gapV)/3.f;
        m_cells.resize(9);
        for (int r=0;r<3;++r)
            for (int c=0;c<3;++c) {
                int i=r*3+c;
                GLuint tid=(i<(int)m_cells.size())?m_cells[i].texId:0;
                m_cells[i]={c*(w+gap), r*(h+gapV), w, h, tid};
            }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
void SplitScreenCompositor::drawCell(const Cell& cell, int outputW, int outputH) {
    // Convert normalized cell rect to NDC
    float ndcX1 = cell.x * 2.f - 1.f;
    float ndcY1 = 1.f - (cell.y + cell.h) * 2.f;
    float ndcX2 = (cell.x + cell.w) * 2.f - 1.f;
    float ndcY2 = 1.f - cell.y * 2.f;

    float verts[16] = {
        ndcX1, ndcY2,  0.f, 1.f,  // TL
        ndcX2, ndcY2,  1.f, 1.f,  // TR
        ndcX1, ndcY1,  0.f, 0.f,  // BL
        ndcX2, ndcY1,  1.f, 0.f,  // BR
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    // Bind texture
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    if (cell.texId) {
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, cell.texId);
    } else {
        // Fill with background color
        // (simplified: just draw black if no texture)
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
    }
    glUniform1i(m_locInputTex, 0);
    glUniform1i(m_locFlipH, cell.flipH ? 1 : 0);
    glUniform1i(m_locFlipV, cell.flipV ? 1 : 0);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
void SplitScreenCompositor::render(GLuint outputFboId, int outputW, int outputH) {
    if (!m_initialized) return;
    buildLayout(m_layout, outputW, outputH);

    glBindFramebuffer(GL_FRAMEBUFFER, outputFboId);
    glViewport(0, 0, outputW, outputH);

    float bg_r = ((m_bgColor>>16)&0xFF)/255.f;
    float bg_g = ((m_bgColor>> 8)&0xFF)/255.f;
    float bg_b = ( m_bgColor     &0xFF)/255.f;
    float bg_a = ((m_bgColor>>24)&0xFF)/255.f;
    glClearColor(bg_r, bg_g, bg_b, bg_a);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().useProgram(m_program);

    for (const auto& cell : m_cells) {
        drawCell(cell, outputW, outputH);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SplitScreenCompositor::render(FrameBufferPtr outputFb) {
    if (!outputFb) return;
    outputFb->bind();
    render(0, outputFb->width(), outputFb->height());
    outputFb->unbind();
}

} // namespace timeline
} // namespace video
} // namespace sdk
