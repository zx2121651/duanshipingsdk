#ifndef LOG_TAG
#define LOG_TAG "StickerRenderer"
#endif
#include "../../include/ai/StickerRenderer.h"
#include "../../include/GLStateManager.h"
#include "../../include/Log.h"
#include <cmath>
#include <cstring>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
const char* StickerRenderer::kVert = R"(#version 300 es
layout(location = 0) in vec2 a_pos;    // unit quad [-1,1]
uniform mat3 u_transform;              // screen-space 3x3 (col-major)
out vec2 v_uv;
void main() {
    vec3 p = u_transform * vec3(a_pos, 1.0);
    gl_Position = vec4(p.xy, 0.0, 1.0);
    v_uv = vec2(a_pos.x * 0.5 + 0.5, 1.0 - (a_pos.y * 0.5 + 0.5));
}
)";

const char* StickerRenderer::kFrag = R"(#version 300 es
precision mediump float;
in  vec2      v_uv;
uniform sampler2D u_texture;
uniform float     u_alpha;
out vec4 fragColor;
void main() {
    vec4 c  = texture(u_texture, v_uv);
    fragColor = vec4(c.rgb, c.a * u_alpha);
}
)";

// Unit quad — 4 vertices × 2 floats, TRIANGLE_STRIP
static const float kQuad[] = {
    -1.f, -1.f,
     1.f, -1.f,
    -1.f,  1.f,
     1.f,  1.f
};

// ---------------------------------------------------------------------------
StickerRenderer::StickerRenderer() = default;
StickerRenderer::~StickerRenderer() { release(); }

Result StickerRenderer::initialize() {
    uint32_t vert = compileShader(GL_VERTEX_SHADER,   kVert);
    uint32_t frag = compileShader(GL_FRAGMENT_SHADER, kFrag);
    m_program = linkProgram(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (!m_program)
        return Result::error(ERR_INIT_SHADER_FAILED, "StickerRenderer shader link failed");

    m_uTransform = glGetUniformLocation(m_program, "u_transform");
    m_uAlpha     = glGetUniformLocation(m_program, "u_alpha");
    m_uTexture   = glGetUniformLocation(m_program, "u_texture");

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_initialized = true;
    LOGI("StickerRenderer initialized");
    return Result::ok();
}

void StickerRenderer::release() {
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    if (m_vbo)     { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Main render entry
// ---------------------------------------------------------------------------
void StickerRenderer::render(const EffectLayerDesc& layer,
                             const EffectPlugin&    plugin,
                             const FaceResult&      face,
                             int64_t                frameTimeMs,
                             int                    viewW,
                             int                    viewH)
{
    if (!m_initialized || !face.detected) return;

    // 1. Choose texture (static or animated frame)
    GLuint texId = 0;
    if (!layer.animFrames.empty()) {
        int frameIdx = resolveAnimFrame(layer, frameTimeMs);
        if (frameIdx >= 0 && frameIdx < (int)layer.animFrames.size())
            texId = plugin.getStickerTexture(layer.animFrames[frameIdx]);
    }
    if (!texId && !layer.assetPath.empty())
        texId = plugin.getStickerTexture(layer.assetPath);
    if (!texId) return;

    // 2. Resolve face anchor → NDC position + rotation + base scale
    float anchorX = 0.f, anchorY = 0.f, headRot = 0.f, faceScale = 1.f;
    if (!resolveAnchor(layer.stickerAnchor.name, face,
                       anchorX, anchorY, headRot, faceScale)) {
        // fallback: forehead midpoint
        const auto& f27 = face.landmarks[27];
        anchorX = f27.x * 2.f - 1.f;
        anchorY = 1.f - f27.y * 2.f;
    }

    // 3. Final scale = face-relative scale × manifest scale × layer intensity
    float aspectRatio = (viewH > 0) ? (float)viewW / (float)viewH : 1.f;
    float scaleX = layer.stickerAnchor.scale * faceScale;
    float scaleY = scaleX * aspectRatio;   // keep square in screen space

    float rotation = layer.stickerAnchor.trackRotation ? headRot : 0.f;

    // 4. Offset in NDC (rotate offset by head angle first)
    float cosR = std::cos(rotation);
    float sinR = std::sin(rotation);
    float offX = layer.stickerAnchor.offsetX;
    float offY = layer.stickerAnchor.offsetY;
    float rotOffX = cosR * offX - sinR * offY;
    float rotOffY = sinR * offX + cosR * offY;
    anchorX += rotOffX;
    anchorY += rotOffY;

    renderAt(texId, anchorX, anchorY,
             (scaleX + scaleY) * 0.5f, rotation,
             layer.intensity, aspectRatio);
}

void StickerRenderer::renderAt(GLuint texId,
                               float  cx, float cy,
                               float  scale, float rot,
                               float  alpha, float /*aspectRatio*/)
{
    if (!m_initialized || !texId) return;

    // Build 3x3 transform (column-major for GL):
    // T(cx,cy) * R(rot) * S(scale)
    float c = std::cos(rot), s = std::sin(rot);
    // col0: [c*scale, s*scale, 0]
    // col1: [-s*scale, c*scale, 0]
    // col2: [cx, cy, 1]
    float mat[9] = {
        c * scale,  s * scale,  0.f,
       -s * scale,  c * scale,  0.f,
        cx,         cy,         1.f
    };

    GLStateManager::getInstance().useProgram(m_program);
    glUniformMatrix3fv(m_uTransform, 1, GL_FALSE, mat);
    glUniform1f(m_uAlpha,   alpha);
    glUniform1i(m_uTexture, 2);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLStateManager::getInstance().activeTexture(GL_TEXTURE2);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, texId);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---------------------------------------------------------------------------
// Anchor resolution
// ---------------------------------------------------------------------------
bool StickerRenderer::resolveAnchor(const std::string& anchorName,
                                    const FaceResult& face,
                                    float& outX, float& outY,
                                    float& outRot, float& outScale)
{
    if (!face.detected) return false;

    // Helpers: landmark → NDC
    auto toNdcX = [](float nx) { return nx * 2.f - 1.f; };
    auto toNdcY = [](float ny) { return 1.f - ny * 2.f; };

    // Head rotation: angle of left-eye → right-eye axis
    const auto& le = face.leftEye();   // landmark 39
    const auto& re = face.rightEye();  // landmark 42
    float dx = re.x - le.x;
    float dy = re.y - le.y;
    outRot = std::atan2(dy, dx);       // radians, negative = head tilted right

    // Face scale: inter-eye distance (in NDC units)
    float eyeDist = std::sqrt(dx * dx + dy * dy);
    outScale = eyeDist * 2.5f;         // empirical multiplier for natural sticker size

    // Select anchor landmark
    if (anchorName == "forehead") {
        // midpoint between brow landmarks (approx): use landmark 19+24 midpoint
        const auto& l19 = face.landmarks[19];
        const auto& l24 = face.landmarks[24];
        outX = toNdcX((l19.x + l24.x) * 0.5f);
        outY = toNdcY((l19.y + l24.y) * 0.5f);
    } else if (anchorName == "leftEye") {
        outX = toNdcX(le.x); outY = toNdcY(le.y);
    } else if (anchorName == "rightEye") {
        outX = toNdcX(re.x); outY = toNdcY(re.y);
    } else if (anchorName == "nose") {
        const auto& n = face.noseTip();
        outX = toNdcX(n.x); outY = toNdcY(n.y);
    } else if (anchorName == "mouth") {
        const auto& ml = face.mouthLeft();
        const auto& mr = face.mouthRight();
        outX = toNdcX((ml.x + mr.x) * 0.5f);
        outY = toNdcY((ml.y + mr.y) * 0.5f);
    } else if (anchorName == "chin") {
        const auto& c = face.chin();
        outX = toNdcX(c.x); outY = toNdcY(c.y);
    } else {
        // Default: nose tip
        const auto& n = face.noseTip();
        outX = toNdcX(n.x); outY = toNdcY(n.y);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Animation frame selection
// ---------------------------------------------------------------------------
int StickerRenderer::resolveAnimFrame(const EffectLayerDesc& layer, int64_t frameTimeMs) {
    int count = static_cast<int>(layer.animFrames.size());
    if (count == 0) return -1;
    if (count == 1) return 0;

    float fps = (layer.animFps > 0.f) ? layer.animFps : 24.f;
    int64_t cycleDurationMs = static_cast<int64_t>((float)count * 1000.f / fps);
    if (cycleDurationMs == 0) return 0;

    int64_t pos = layer.animLoop
        ? (frameTimeMs % cycleDurationMs)
        : std::min(frameTimeMs, cycleDurationMs - 1);

    return static_cast<int>((float)pos / 1000.f * fps);
}

// ---------------------------------------------------------------------------
// Shader helpers
// ---------------------------------------------------------------------------
uint32_t StickerRenderer::compileShader(uint32_t type, const char* src) {
    uint32_t s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        LOGE("StickerRenderer shader error: %s", log);
        glDeleteShader(s); return 0;
    }
    return s;
}

uint32_t StickerRenderer::linkProgram(uint32_t vert, uint32_t frag) {
    if (!vert || !frag) return 0;
    uint32_t p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(p, 512, nullptr, log);
        LOGE("StickerRenderer link error: %s", log);
        glDeleteProgram(p); return 0;
    }
    return p;
}

} // namespace ai
} // namespace video
} // namespace sdk
