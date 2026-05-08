/**
 * TemplateEngine.cpp
 *
 * 模板解析 + Timeline 生成。
 *
 * 依赖：
 *   - 无第三方 JSON 库（手写简单解析器，适配模板 schema）
 *   - Timeline / Track / Clip / SubtitleClip（SDK 内部）
 */

#include "../../include/timeline/TemplateEngine.h"
#include "../../include/timeline/Track.h"
#include "../../include/timeline/Clip.h"
#include "../../include/timeline/SubtitleClip.h"

#define LOG_TAG "TemplateEngine"
#include "../../include/Log.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
#   include <windows.h>
#   include <io.h>
#else
#   include <dirent.h>
#endif

namespace sdk {
namespace video {
namespace timeline {

// ============================================================================
// Mini JSON parser — supports the template schema without external deps
// ============================================================================
namespace {

static constexpr int64_t kMs2Ns = 1'000'000LL;

// ─── token types ──────────────────────────────────────────────────────────
enum TokType { TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
               TOK_COLON, TOK_COMMA, TOK_STRING, TOK_NUMBER, TOK_BOOL,
               TOK_NULL, TOK_EOF, TOK_ERROR };
struct Token {
    TokType     type  = TOK_ERROR;
    std::string sval;      // string / key value
    double      nval = 0;  // number value
    bool        bval = false;
};

class JsonReader {
public:
    explicit JsonReader(const std::string& src) : m_src(src), m_pos(0) {}

    Token next() {
        skipWs();
        if (m_pos >= m_src.size()) return {TOK_EOF};
        char c = m_src[m_pos];
        switch (c) {
            case '{': ++m_pos; return {TOK_LBRACE};
            case '}': ++m_pos; return {TOK_RBRACE};
            case '[': ++m_pos; return {TOK_LBRACKET};
            case ']': ++m_pos; return {TOK_RBRACKET};
            case ':': ++m_pos; return {TOK_COLON};
            case ',': ++m_pos; return {TOK_COMMA};
            case '"': return readString();
            case 't': case 'f': return readBool();
            case 'n': return readNull();
            default:
                if (c == '-' || std::isdigit((unsigned char)c)) return readNumber();
                return {TOK_ERROR};
        }
    }

    // Peek without consuming
    Token peek() {
        size_t saved = m_pos;
        Token t = next();
        m_pos = saved;
        return t;
    }

private:
    const std::string& m_src;
    size_t m_pos;

    void skipWs() {
        while (m_pos < m_src.size() && std::isspace((unsigned char)m_src[m_pos]))
            ++m_pos;
    }

    Token readString() {
        ++m_pos; // skip "
        std::string val;
        while (m_pos < m_src.size() && m_src[m_pos] != '"') {
            if (m_src[m_pos] == '\\' && m_pos + 1 < m_src.size()) {
                char esc = m_src[m_pos + 1];
                switch (esc) {
                    case '"':  val += '"';  break;
                    case '\\': val += '\\'; break;
                    case '/':  val += '/';  break;
                    case 'n':  val += '\n'; break;
                    case 't':  val += '\t'; break;
                    case 'r':  val += '\r'; break;
                    default:   val += esc;  break;
                }
                m_pos += 2;
            } else {
                val += m_src[m_pos++];
            }
        }
        if (m_pos < m_src.size()) ++m_pos; // skip closing "
        return {TOK_STRING, val};
    }

    Token readNumber() {
        size_t start = m_pos;
        if (m_src[m_pos] == '-') ++m_pos;
        while (m_pos < m_src.size() &&
               (std::isdigit((unsigned char)m_src[m_pos]) ||
                m_src[m_pos] == '.' || m_src[m_pos] == 'e' ||
                m_src[m_pos] == 'E' || m_src[m_pos] == '+' ||
                m_src[m_pos] == '-')) {
            ++m_pos;
        }
        std::string ns = m_src.substr(start, m_pos - start);
        Token t;
        t.type  = TOK_NUMBER;
        t.sval  = ns;
        t.nval  = std::stod(ns);
        return t;
    }

    Token readBool() {
        Token t; t.type = TOK_BOOL;
        if (m_src.compare(m_pos, 4, "true") == 0)  { t.bval = true;  m_pos += 4; }
        else if (m_src.compare(m_pos, 5, "false") == 0) { t.bval = false; m_pos += 5; }
        else t.type = TOK_ERROR;
        return t;
    }

    Token readNull() {
        if (m_src.compare(m_pos, 4, "null") == 0) { m_pos += 4; return {TOK_NULL}; }
        return {TOK_ERROR};
    }
};

// ─── helper: consume expected token ──────────────────────────────────────
static bool expect(JsonReader& rd, TokType t, std::string& err) {
    Token got = rd.next();
    if (got.type != t) {
        err = "JSON parse error: unexpected token";
        return false;
    }
    return true;
}

// ─── parse slot object ────────────────────────────────────────────────────
static bool parseSlot(JsonReader& rd, TemplateSlot& slot, std::string& err) {
    // Already consumed '{'
    while (true) {
        Token k = rd.next();
        if (k.type == TOK_RBRACE) break;
        if (k.type == TOK_COMMA)  { k = rd.next(); }
        if (k.type != TOK_STRING) { err = "slot: expected key"; return false; }
        if (!expect(rd, TOK_COLON, err)) return false;
        Token v = rd.next();

        if (k.sval == "id")           slot.id           = v.sval;
        else if (k.sval == "type")    slot.type         = v.sval;
        else if (k.sval == "duration_ms") slot.durationMs = (int64_t)v.nval;
        else if (k.sval == "transition")  slot.transition = v.sval;
        else if (k.sval == "trans_dur_ms") slot.transDurMs = (int64_t)v.nval;
        else if (k.sval == "effect")  slot.effectId     = v.sval;
        else if (k.sval == "fit")     slot.fitMode      = slotFitModeFromString(v.sval);
        // ignore unknown keys
    }
    return true;
}

// ─── parse subtitle object ────────────────────────────────────────────────
static bool parseSubtitle(JsonReader& rd, TemplateSubtitle& sub, std::string& err) {
    while (true) {
        Token k = rd.next();
        if (k.type == TOK_RBRACE) break;
        if (k.type == TOK_COMMA)  { k = rd.next(); }
        if (k.type != TOK_STRING) { err = "subtitle: expected key"; return false; }
        if (!expect(rd, TOK_COLON, err)) return false;
        Token v = rd.next();

        if      (k.sval == "id")        sub.id         = v.sval;
        else if (k.sval == "text")      sub.text       = v.sval;
        else if (k.sval == "start_ms")  sub.startMs    = (int64_t)v.nval;
        else if (k.sval == "end_ms")    sub.endMs      = (int64_t)v.nval;
        else if (k.sval == "font_size") sub.fontSizePx = (float)v.nval;
        else if (k.sval == "color")     sub.textColor  = (uint32_t)(unsigned long long)v.nval;
        else if (k.sval == "x")         sub.x          = (float)v.nval;
        else if (k.sval == "y")         sub.y          = (float)v.nval;
        else if (k.sval == "align")     sub.alignment  = (int)v.nval;
    }
    return true;
}

// ─── parse audio object ───────────────────────────────────────────────────
static bool parseAudio(JsonReader& rd, TemplateAudio& audio, std::string& err) {
    while (true) {
        Token k = rd.next();
        if (k.type == TOK_RBRACE) break;
        if (k.type == TOK_COMMA)  { k = rd.next(); }
        if (k.type != TOK_STRING) { err = "audio: expected key"; return false; }
        if (!expect(rd, TOK_COLON, err)) return false;
        Token v = rd.next();

        if      (k.sval == "type")   audio.type   = v.sval;
        else if (k.sval == "path")   audio.path   = v.sval;
        else if (k.sval == "loop")   audio.loop   = v.bval;
        else if (k.sval == "volume") audio.volume = (float)v.nval;
    }
    return true;
}

// ─── parse lut object ─────────────────────────────────────────────────────
static bool parseLut(JsonReader& rd, TemplateLut& lut, std::string& err) {
    while (true) {
        Token k = rd.next();
        if (k.type == TOK_RBRACE) break;
        if (k.type == TOK_COMMA)  { k = rd.next(); }
        if (k.type != TOK_STRING) { err = "lut: expected key"; return false; }
        if (!expect(rd, TOK_COLON, err)) return false;
        Token v = rd.next();

        if      (k.sval == "path")      lut.path      = v.sval;
        else if (k.sval == "intensity") lut.intensity = (float)v.nval;
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// TemplateEngine::parseJson
// ============================================================================
bool TemplateEngine::parseJson(const std::string& json, VideoTemplate& out,
                               std::string& errOut)
{
    JsonReader rd(json);
    Token t = rd.next();
    if (t.type != TOK_LBRACE) { errOut = "expected top-level object"; return false; }

    while (true) {
        Token k = rd.next();
        if (k.type == TOK_RBRACE) break;
        if (k.type == TOK_COMMA)  { k = rd.next(); }
        if (k.type == TOK_RBRACE) break;
        if (k.type != TOK_STRING) { errOut = "expected key string"; return false; }
        if (!expect(rd, TOK_COLON, errOut)) return false;

        if (k.sval == "id")          { Token v = rd.next(); out.id          = v.sval; }
        else if (k.sval == "name")   { Token v = rd.next(); out.name        = v.sval; }
        else if (k.sval == "version"){ Token v = rd.next(); out.version     = (int)v.nval; }
        else if (k.sval == "duration_ms") { Token v = rd.next(); out.durationMs = (int64_t)v.nval; }
        else if (k.sval == "fps")    { Token v = rd.next(); out.fps         = (int)v.nval; }
        else if (k.sval == "width")  { Token v = rd.next(); out.width       = (int)v.nval; }
        else if (k.sval == "height") { Token v = rd.next(); out.height      = (int)v.nval; }
        else if (k.sval == "slots") {
            if (!expect(rd, TOK_LBRACKET, errOut)) return false;
            while (true) {
                Token peek = rd.peek();
                if (peek.type == TOK_RBRACKET) { rd.next(); break; }
                if (peek.type == TOK_COMMA)    { rd.next(); continue; }
                if (peek.type != TOK_LBRACE)   { errOut = "slots: expected object"; return false; }
                rd.next(); // consume '{'
                TemplateSlot slot;
                if (!parseSlot(rd, slot, errOut)) return false;
                out.slots.push_back(std::move(slot));
            }
        }
        else if (k.sval == "subtitles") {
            if (!expect(rd, TOK_LBRACKET, errOut)) return false;
            while (true) {
                Token peek = rd.peek();
                if (peek.type == TOK_RBRACKET) { rd.next(); break; }
                if (peek.type == TOK_COMMA)    { rd.next(); continue; }
                if (peek.type != TOK_LBRACE)   { errOut = "subtitles: expected object"; return false; }
                rd.next();
                TemplateSubtitle sub;
                if (!parseSubtitle(rd, sub, errOut)) return false;
                out.subtitles.push_back(std::move(sub));
            }
        }
        else if (k.sval == "audio") {
            if (!expect(rd, TOK_LBRACE, errOut)) return false;
            if (!parseAudio(rd, out.audio, errOut)) return false;
        }
        else if (k.sval == "lut") {
            if (!expect(rd, TOK_LBRACE, errOut)) return false;
            if (!parseLut(rd, out.lut, errOut)) return false;
        }
        else {
            // skip unknown value (could be any type — just consume one token)
            rd.next();
        }
    }
    return true;
}

// ============================================================================
// loadFromString / loadFromFile
// ============================================================================
bool TemplateEngine::loadFromString(const std::string& json, VideoTemplate& outTmpl) {
    std::string err;
    if (!parseJson(json, outTmpl, err)) {
        LOGE("TemplateEngine::loadFromString: %s", err.c_str());
        return false;
    }
    return true;
}

bool TemplateEngine::loadFromFile(const std::string& filePath, VideoTemplate& outTmpl) {
    std::ifstream f(filePath);
    if (!f.is_open()) {
        LOGE("TemplateEngine::loadFromFile: cannot open %s", filePath.c_str());
        return false;
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    return loadFromString(buf.str(), outTmpl);
}

// ============================================================================
// apply() — VideoTemplate → Timeline
// ============================================================================
std::shared_ptr<Timeline> TemplateEngine::apply(const VideoTemplate& tmpl) {
    if (tmpl.slots.empty()) {
        m_lastError = "TemplateEngine::apply: template has no slots";
        LOGE("%s", m_lastError.c_str());
        return nullptr;
    }

    auto timeline = std::make_shared<Timeline>(tmpl.width, tmpl.height, tmpl.fps);

    // ── 1. 主视频轨 ───────────────────────────────────────────────────────
    auto videoTrack = timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);
    int64_t cursorNs = 0;

    for (const auto& slot : tmpl.slots) {
        const std::string& path = slot.filledPath.empty()
            ? ("__placeholder__" + slot.id)  // 未填充时用占位符路径
            : slot.filledPath;

        auto clip = std::make_shared<Clip>(slot.id, path, Clip::MediaType::VIDEO);
        int64_t durNs = slot.durationMs * kMs2Ns;
        clip->setSourceDuration(durNs);
        clip->setTrimOut(durNs);
        clip->setTimelineIn(cursorNs);

        // 应用入场转场
        if (!slot.transition.empty() && slot.transition != "none") {
            clip->setInTransitionByName(slot.transition,
                                        slot.transDurMs * kMs2Ns);
        }

        videoTrack->addClip(clip);
        cursorNs += durNs;
    }

    // ── 2. 音频轨（可选）──────────────────────────────────────────────────
    if (tmpl.audio.type == "music" && !tmpl.audio.path.empty()) {
        auto audioTrack = timeline->addTrack(1, Track::TrackType::AUDIO_ONLY);
        auto audioClip = std::make_shared<Clip>(
            "audio_bg", tmpl.audio.path, Clip::MediaType::AUDIO);
        int64_t totalNs = tmpl.durationMs * kMs2Ns;
        audioClip->setSourceDuration(totalNs);
        audioClip->setTrimOut(totalNs);
        audioClip->setTimelineIn(0);
        audioClip->setVolume(tmpl.audio.volume);
        audioTrack->addClip(audioClip);
    }

    // ── 3. 字幕轨（可选）──────────────────────────────────────────────────
    if (!tmpl.subtitles.empty()) {
        auto subTrack = timeline->addTrack(10, Track::TrackType::SUBTITLE);
        for (const auto& s : tmpl.subtitles) {
            auto sub = std::make_shared<SubtitleClip>(s.id, s.text);
            int64_t durNs = (s.endMs - s.startMs) * kMs2Ns;
            sub->setSourceDuration(durNs);
            sub->setTrimOut(durNs);
            sub->setTimelineIn(s.startMs * kMs2Ns);
            sub->style.fontSizePx = s.fontSizePx;
            sub->style.textColor  = s.textColor;
            sub->style.x          = s.x;
            sub->style.y          = s.y;
            sub->style.alignment  = s.alignment;
            subTrack->addClip(sub);
        }
    }

    LOGI("TemplateEngine::apply: built timeline from '%s' (%d slots, %d subtitles)",
         tmpl.id.c_str(), (int)tmpl.slots.size(), (int)tmpl.subtitles.size());
    return timeline;
}

// ============================================================================
// loadAllFromDirectory
// ============================================================================
std::vector<VideoTemplate> TemplateEngine::loadAllFromDirectory(
    const std::string& dirPath)
{
    std::vector<VideoTemplate> result;

#ifdef _WIN32
    std::string pattern = dirPath + "\\*.json";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return result;
    do {
        std::string fullPath = dirPath + "\\" + ffd.cFileName;
        VideoTemplate tmpl;
        if (loadFromFile(fullPath, tmpl)) result.push_back(std::move(tmpl));
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
#else
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return result;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() < 5) continue;
        if (name.substr(name.size() - 5) != ".json") continue;
        std::string fullPath = dirPath + "/" + name;
        VideoTemplate tmpl;
        if (loadFromFile(fullPath, tmpl)) result.push_back(std::move(tmpl));
    }
    closedir(dir);
#endif

    return result;
}

} // namespace timeline
} // namespace video
} // namespace sdk
