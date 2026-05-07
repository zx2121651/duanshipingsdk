#ifndef LOG_TAG
#define LOG_TAG "PropOverlayFilter"
#endif
#include "../include/PropOverlayFilter.h"
#include "../include/FrameBuffer.h"
#include "../include/GLStateManager.h"
#include "../include/Log.h"
#include <cmath>
#include <cstring>
#include <any>
#include <string>

namespace sdk {
namespace video {

// ---------------------------------------------------------------------------
// Shader sources
// ---------------------------------------------------------------------------

// ── Passthrough: copy input texture to output FBO ────────────────────────
const char* PropOverlayFilter::kPassthroughVert = R"(#version 300 es
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
out vec2 v_texCoord;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord  = a_texCoord;
}
)";

const char* PropOverlayFilter::kPassthroughFrag = R"(#version 300 es
precision mediump float;
in  vec2      v_texCoord;
uniform sampler2D s_texture;
out vec4 fragColor;
void main() {
    fragColor = texture(s_texture, v_texCoord);
}
)";

// ── Prop sprite: alpha-blended RGBA quad ─────────────────────────────────
const char* PropOverlayFilter::kPropVert = R"(#version 300 es
layout(location = 0) in vec2 a_position;   // unit quad [-1, 1]
uniform vec2  u_center;    // NDC center
uniform float u_scale;
uniform float u_rotation;
out vec2 v_uv;
void main() {
    float c = cos(u_rotation);
    float s = sin(u_rotation);
    // rotate then scale+translate
    vec2 rotated = vec2(
        c * a_position.x - s * a_position.y,
        s * a_position.x + c * a_position.y
    );
    gl_Position = vec4(rotated * u_scale + u_center, 0.0, 1.0);
    // map [-1,1] -> [0,1] UV, flip V to match texture origin
    v_uv = vec2(a_position.x * 0.5 + 0.5,
                1.0 - (a_position.y * 0.5 + 0.5));
}
)";

const char* PropOverlayFilter::kPropFrag = R"(#version 300 es
precision mediump float;
in  vec2      v_uv;
uniform sampler2D s_propTexture;
uniform float     u_alpha;
out vec4 fragColor;
void main() {
    vec4 color = texture(s_propTexture, v_uv);
    fragColor  = vec4(color.rgb, color.a * u_alpha);
}
)";

// ---------------------------------------------------------------------------
// Unit quad geometry: position(x,y) + texCoord(u,v)  —  4 vertices, TRIANGLE_STRIP
// ---------------------------------------------------------------------------
static const float kQuadData[] = {
    // x,    y,    u,    v
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
};

// For the prop sprite we only need position (no UV in VBO — UV derived in shader)
static const float kPropQuad[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
};

// ---------------------------------------------------------------------------
PropOverlayFilter::PropOverlayFilter() {
    // zero-init all prop slots
    for (auto& p : m_props) p = Prop{};
}

// ---------------------------------------------------------------------------
Result PropOverlayFilter::initialize() {
    // Skip base Filter::initialize() — we manage our own programs.
    compilePrograms();
    if (!m_passthroughProgram || !m_propProgram) {
        return Result::error(static_cast<int>(ERR_INIT_SHADER_FAILED),
                             "PropOverlayFilter: shader compilation failed");
    }

    // Upload quad VBO once
    glGenBuffers(1, &m_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadData), kQuadData, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Cache uniform locations for prop shader
    m_uPropCenter   = glGetUniformLocation(m_propProgram, "u_center");
    m_uPropScale    = glGetUniformLocation(m_propProgram, "u_scale");
    m_uPropRotation = glGetUniformLocation(m_propProgram, "u_rotation");
    m_uPropAlpha    = glGetUniformLocation(m_propProgram, "u_alpha");
    m_uPropTexture  = glGetUniformLocation(m_propProgram, "s_propTexture");

    LOGI("PropOverlayFilter initialized");
    return Result::ok();
}

// ---------------------------------------------------------------------------
int PropOverlayFilter::addProp(uint32_t texId, float x, float y,
                               float scale, float rotation, float alpha) {
    for (int i = 0; i < MAX_PROPS; ++i) {
        if (!m_props[i].active) {
            m_props[i] = { texId, x, y, scale, rotation, alpha, true };
            LOGI("Prop added slot=%d texId=%u", i, texId);
            return i;
        }
    }
    LOGW("addProp: all %d slots are occupied", MAX_PROPS);
    return -1;
}

void PropOverlayFilter::removeProp(int slot) {
    if (slot < 0 || slot >= MAX_PROPS) return;
    m_props[slot].active = false;
    m_props[slot].texId  = 0;
}

void PropOverlayFilter::clearProps() {
    for (auto& p : m_props) p = Prop{};
}

void PropOverlayFilter::updateProp(int slot, const Prop& prop) {
    if (slot < 0 || slot >= MAX_PROPS) return;
    m_props[slot] = prop;
}

const PropOverlayFilter::Prop& PropOverlayFilter::getProp(int slot) const {
    static const Prop kInvalid{};
    if (slot < 0 || slot >= MAX_PROPS) return kInvalid;
    return m_props[slot];
}

// ---------------------------------------------------------------------------
// setParameter — "prop<N>.<field>" parsing
// ---------------------------------------------------------------------------
void PropOverlayFilter::setParameter(const std::string& key, const std::any& value) {
    // Try to parse "prop<N>.<field>"
    if (key.size() < 6 || key.substr(0, 4) != "prop") {
        Filter::setParameter(key, value);
        return;
    }
    size_t dotPos = key.find('.', 4);
    if (dotPos == std::string::npos) { Filter::setParameter(key, value); return; }

    int slot = -1;
    try { slot = std::stoi(key.substr(4, dotPos - 4)); } catch (...) { return; }
    if (slot < 0 || slot >= MAX_PROPS) return;

    const std::string field = key.substr(dotPos + 1);
    auto& p = m_props[slot];

    if      (field == "texId")    { p.texId    = static_cast<uint32_t>(std::any_cast<float>(value)); p.active = (p.texId != 0); }
    else if (field == "x")        { p.centerX  = std::any_cast<float>(value); }
    else if (field == "y")        { p.centerY  = std::any_cast<float>(value); }
    else if (field == "scale")    { p.scale    = std::any_cast<float>(value); }
    else if (field == "rotation") { p.rotation = std::any_cast<float>(value); }
    else if (field == "alpha")    { p.alpha    = std::any_cast<float>(value); }
    else if (field == "active")   { p.active   = (std::any_cast<int>(value) != 0); }
}

// ---------------------------------------------------------------------------
// onDraw — two-pass rendering
// ---------------------------------------------------------------------------
void PropOverlayFilter::onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) {
    drawPassthrough(inputTexture, outputFb);
    drawPropSprites(outputFb);
}

// Pass 1: copy input frame to output FBO
void PropOverlayFilter::drawPassthrough(const Texture& inputTexture, FrameBufferPtr outputFb) {
    outputFb->bind();
    glViewport(0, 0, static_cast<GLsizei>(inputTexture.width),
                     static_cast<GLsizei>(inputTexture.height));
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLStateManager::getInstance().useProgram(m_passthroughProgram);

    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    // layout(location=0) a_position  — stride 4 floats, offset 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(0));
    // layout(location=1) a_texCoord  — stride 4 floats, offset 2 floats
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, inputTexture.id);
    glUniform1i(glGetUniformLocation(m_passthroughProgram, "s_texture"), 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Pass 2: alpha-blend each active prop sprite on top
void PropOverlayFilter::drawPropSprites(FrameBufferPtr outputFb) {
    bool anyActive = false;
    for (const auto& p : m_props) {
        if (p.active && p.texId != 0) { anyActive = true; break; }
    }
    if (!anyActive) return;

    outputFb->bind(); // still current FBO

    GLStateManager::getInstance().useProgram(m_propProgram);

    // Upload prop quad (position only)
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glEnableVertexAttribArray(0);
    // prop quad positions occupy first 2 floats of each 4-float vertex
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(0));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUniform1i(m_uPropTexture, 1); // texture unit 1

    for (const auto& p : m_props) {
        if (!p.active || p.texId == 0) continue;

        glUniform2f(m_uPropCenter,   p.centerX, p.centerY);
        glUniform1f(m_uPropScale,    p.scale);
        glUniform1f(m_uPropRotation, p.rotation);
        glUniform1f(m_uPropAlpha,    p.alpha);

        GLStateManager::getInstance().activeTexture(GL_TEXTURE1);
        GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, p.texId);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---------------------------------------------------------------------------
// Shader compilation helpers
// ---------------------------------------------------------------------------
void PropOverlayFilter::compilePrograms() {
    uint32_t ptv = compileShader(GL_VERTEX_SHADER,   kPassthroughVert);
    uint32_t ptf = compileShader(GL_FRAGMENT_SHADER, kPassthroughFrag);
    m_passthroughProgram = linkProgram(ptv, ptf);
    glDeleteShader(ptv);
    glDeleteShader(ptf);

    uint32_t pv = compileShader(GL_VERTEX_SHADER,   kPropVert);
    uint32_t pf = compileShader(GL_FRAGMENT_SHADER, kPropFrag);
    m_propProgram = linkProgram(pv, pf);
    glDeleteShader(pv);
    glDeleteShader(pf);
}

uint32_t PropOverlayFilter::compileShader(uint32_t type, const char* src) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(shader, 512, nullptr, log);
        LOGE("Shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

uint32_t PropOverlayFilter::linkProgram(uint32_t vert, uint32_t frag) {
    if (!vert || !frag) return 0;
    uint32_t prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(prog, 512, nullptr, log);
        LOGE("Program link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ---------------------------------------------------------------------------
// Stubs for pure-virtual Filter methods not used in this subclass
// ---------------------------------------------------------------------------
std::string PropOverlayFilter::getFragmentShaderSource() const { return ""; }

} // namespace video
} // namespace sdk
