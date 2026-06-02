#pragma once
// TimelineDraft.h — header-only serialize / deserialize for Timeline drafts.
// Format: line-based text, ':'-delimited fields. No external deps required.
//
// Schema v2 (one line per entity):
//   SVDK_DRAFT:<version>:<width>:<height>:<fps>
//   T:<zIndex>:<type>:<opacity>:<volume>
//   C:<id>:<srcPath>:<mediaType>:<srcDurNs>:<trimInNs>:<trimOutNs>:<tlInNs>:
//     <speed>:<volume>:<reversed>:<scale>:<rotation>:<tx>:<ty>:
//     <inTransition>:<inTransDurNs>:<outTransition>:<outTransDurNs>
//   K:<clipId>:<param>:<timeNs>:<value>:<easingInt>   (* easing new in v2)
//   SUB:<clipId>:<text>:<tlInNs>:<srcDurNs>:<x>:<y>:<fontSize>:<textColor>:<bgColor>:<align>
//   STICKER:<clipId>:<srcPath>:<srcDurNs>:<tlInNs>:<centerX>:<centerY>:<scale>:<rotation>

#include "Timeline.h"
#include "Track.h"
#include "Clip.h"
#include "SubtitleClip.h"
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstring>

namespace sdk {
namespace video {
namespace timeline {

static constexpr int DRAFT_FORMAT_VERSION = 3;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace detail {

inline std::vector<std::string> splitLine(const std::string& line, char delim) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : line) {
        if (c == delim) { parts.push_back(cur); cur.clear(); }
        else            { cur += c; }
    }
    parts.push_back(cur);
    return parts;
}

inline std::string escapeColons(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == ':') out += "\\c";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

inline std::string unescapeColons(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i+1];
            if (n == 'c') { out += ':'; ++i; }
            else if (n == '\\') { out += '\\'; ++i; }
            else if (n == 'n') { out += '\n'; ++i; }
            else out += s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

inline std::string normalizePath(const std::string& path) {
    std::string out = path;
    for (char& c : out) {
        if (c == '\\') c = '/';
    }
    return out;
}

inline std::string getDirectoryPath(const std::string& filePath) {
    std::string norm = normalizePath(filePath);
    size_t lastSlash = norm.find_last_of('/');
    if (lastSlash == std::string::npos) {
        return "";
    }
    return norm.substr(0, lastSlash + 1); // include trailing slash
}

inline std::string detectSandboxPath(const std::string& filePath) {
    std::string norm = normalizePath(filePath);
    static const char* patterns[] = {
        "/files/", "/cache/", "/Documents/", "/Library/", "/tmp/"
    };
    for (const char* pattern : patterns) {
        size_t pos = norm.find(pattern);
        if (pos != std::string::npos) {
            // Include the pattern itself up to the trailing slash
            return norm.substr(0, pos + strlen(pattern));
        }
    }
    return "";
}

inline bool startsWithIgnoreCase(const std::string& str, const std::string& prefix) {
    if (str.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(str[i]) != std::tolower(prefix[i])) {
            return false;
        }
    }
    return true;
}

inline std::string serializePath(const std::string& path, const std::string& draftRoot, const std::string& sandboxRoot) {
    if (path.empty()) return path;
    
    std::string normPath = normalizePath(path);
    std::string normDraftRoot = normalizePath(draftRoot);
    std::string normSandboxRoot = normalizePath(sandboxRoot);
    
    if (!normDraftRoot.empty() && startsWithIgnoreCase(normPath, normDraftRoot)) {
        return "<DRAFT_ROOT>/" + normPath.substr(normDraftRoot.size());
    }
    if (!normSandboxRoot.empty() && startsWithIgnoreCase(normPath, normSandboxRoot)) {
        return "<SANDBOX>/" + normPath.substr(normSandboxRoot.size());
    }
    // External path: return normalized (forward-slash) form so the colon in
    // Windows drive letters (e.g. "D:/") is NOT double-escaped later.
    return normPath;
}

inline std::string deserializePath(const std::string& path, const std::string& draftRoot, const std::string& sandboxRoot) {
    if (path.empty()) return path;
    
    if (path.compare(0, 13, "<DRAFT_ROOT>/") == 0) {
        return normalizePath(draftRoot) + path.substr(13);
    }
    if (path.compare(0, 10, "<SANDBOX>/") == 0) {
        return normalizePath(sandboxRoot) + path.substr(10);
    }
    return path;
}

} // namespace detail

// ---------------------------------------------------------------------------
// serialize helpers
// ---------------------------------------------------------------------------
namespace detail {

inline void serializeClip(std::ostringstream& ss, const ClipPtr& clip, const std::string& draftRoot, const std::string& sandboxRoot) {
    const std::string& clipId = clip->getId();
    // ── Check for specialised subtypes ────────────────────────────────────
    if (auto sub = std::dynamic_pointer_cast<SubtitleClip>(clip)) {
        // SUB:<id>:<text>:<tlIn>:<srcDur>:<x>:<y>:<fontSize>:<textColor>:<bgColor>:<align>
        ss << "SUB:"
           << escapeColons(clipId)                   << ":"
           << escapeColons(sub->getText())           << ":"
           << sub->getTimelineIn()                   << ":"
           << sub->getSourceDuration()               << ":"
           << sub->style.x                           << ":"
           << sub->style.y                           << ":"
           << sub->style.fontSizePx                  << ":"
           << sub->style.textColor                   << ":"
           << sub->style.bgColor                     << ":"
           << sub->style.alignment                   << "\n";
        return;
    }
    if (auto stk = std::dynamic_pointer_cast<StickerClip>(clip)) {
        std::string serializedPath = serializePath(stk->getSourcePath(), draftRoot, sandboxRoot);
        // STICKER:<id>:<srcPath>:<srcDur>:<tlIn>:<cx>:<cy>:<scale>:<rot>
        // NOTE: path is stored verbatim (not colon-escaped) so Windows drive letters
        //       such as "D:/" are preserved literally. Deserialization handles this.
        ss << "STICKER:"
           << escapeColons(clipId)                   << ":"
           << serializedPath                         << ":"
           << stk->getSourceDuration()               << ":"
           << stk->getTimelineIn()                   << ":"
           << stk->centerX                           << ":"
           << stk->centerY                           << ":"
           << stk->stickerScale                      << ":"
           << stk->stickerRotation                   << "\n";
        return;
    }
    // ── Generic Clip ─────────────────────────────────────────────────────────
    std::string serializedPath = serializePath(clip->getSourcePath(), draftRoot, sandboxRoot);
    // NOTE: path is stored verbatim (not colon-escaped) so that Windows drive
    //       letters such as "D:/" appear literally in the draft file.
    //       Deserialization detects and re-joins the split drive-letter field.
    ss << "C:"
       << escapeColons(clipId)                          << ":"
       << serializedPath                                << ":"  // no escapeColons on path
       << static_cast<int>(clip->getType())             << ":"
       << clip->getSourceDuration()                     << ":"
       << clip->getTrimIn()                             << ":"
       << clip->getTrimOut()                            << ":"
       << clip->getTimelineIn()                         << ":"
       << clip->getSpeed()                              << ":"
       << clip->getVolume(0)                            << ":"
       << (clip->isReversed() ? 1 : 0)                 << ":"
       << clip->getScale(0)                             << ":"
       << 0.0f << ":" << 0.0f << ":" << 0.0f            << ":"
       << escapeColons(clip->getInTransitionName())     << ":"
       << clip->getInTransitionDurationNs()             <<             ":"
       << escapeColons(clip->getOutTransitionName())    << ":"
       << clip->getOutTransitionDurationNs()            << "\n";

    // ── Keyframes (emit K: lines for every param that has KFs) ───────────
    static const char* kTrackedParams[] = {
        "opacity", "volume", "scale", "posX", "posY", "rotation", nullptr
    };
    for (int pi = 0; kTrackedParams[pi]; ++pi) {
        const char* param = kTrackedParams[pi];
        auto kfs = clip->getKeyframes(param);
        for (const auto& [timeNs, entry] : kfs) {
            ss << "K:"
               << escapeColons(clipId) << ":"
               << param               << ":"
               << timeNs              << ":"
               << entry.value         << ":"
               << static_cast<int>(entry.easing) << ":"
               << entry.cp1x          << ":"
               << entry.cp1y          << ":"
               << entry.cp2x          << ":"
               << entry.cp2y          << "\n";
        }
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// serialize
// ---------------------------------------------------------------------------
inline std::string serializeTimeline(const Timeline& tl, const std::string& draftRoot = "", const std::string& sandboxRoot = "") {
    std::ostringstream ss;
    ss << "SVDK_DRAFT:" << DRAFT_FORMAT_VERSION << ":"
       << tl.getOutputWidth() << ":" << tl.getOutputHeight() << ":" << tl.getFps() << "\n";

    for (int z = 0; z <= 255; ++z) {
        TrackPtr track = tl.getTrack(z);
        if (!track) continue;

        ss << "T:" << z << ":" << static_cast<int>(track->getType()) << ":"
           << track->getOpacity() << ":" << track->getTrackVolume() << "\n";

        // O(n) direct iteration via getAllClips()
        for (const auto& clip : track->getAllClips())
            detail::serializeClip(ss, clip, draftRoot, sandboxRoot);
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// saveDraft / loadDraft  (file I/O wrappers)
// ---------------------------------------------------------------------------
inline std::shared_ptr<Timeline> deserializeTimeline(const std::string& content, const std::string& draftRoot = "", const std::string& sandboxRoot = "") {
    std::istringstream ss(content);
    std::string line;

    std::shared_ptr<Timeline> timeline;
    TrackPtr currentTrack;
    int      currentZIndex = -1;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        auto parts = detail::splitLine(line, ':');
        if (parts.empty()) continue;

        const std::string& tag = parts[0];

        if (tag == "SVDK_DRAFT") {
            if (parts.size() < 5) return nullptr;
            // int version = std::stoi(parts[1]); // reserved for migration
            int w   = std::stoi(parts[2]);
            int h   = std::stoi(parts[3]);
            int fps = std::stoi(parts[4]);
            timeline = std::make_shared<Timeline>(w, h, fps);

        } else if (tag == "T" && timeline) {
            if (parts.size() < 5) continue;
            int zIndex = std::stoi(parts[1]);
            Track::TrackType type = static_cast<Track::TrackType>(std::stoi(parts[2]));
            float opacity = std::stof(parts[3]);
            float volume  = std::stof(parts[4]);
            currentTrack   = timeline->addTrack(zIndex, type);
            currentZIndex  = zIndex;
            if (currentTrack) {
                currentTrack->setOpacity(opacity);
                currentTrack->setTrackVolume(volume);
            }

        } else if (tag == "C" && timeline && currentTrack) {
            // C:id:src:mediaType:srcDur:trimIn:trimOut:tlIn:speed:vol:rev:
            //   scale:rotation:tx:ty:inT:inTDur:outT:outTDur
            // NOTE: the path field is stored verbatim (no colon-escaping), so a
            // Windows absolute path like "D:/movies/a.mp4" creates an extra ":".
            // We detect this by checking whether parts[2] is a single drive letter.
            std::string id = detail::unescapeColons(parts[1]);
            std::string src;
            int fo = 0; // field offset caused by Windows drive-letter colon
            if (parts.size() > 3
                && parts[2].size() == 1
                && std::isalpha((unsigned char)parts[2][0])
                && !parts[3].empty() && parts[3][0] == '/') {
                // New format: drive letter split → rejoin as "X:/rest"
                src = std::string(1, parts[2][0]) + ":" + parts[3];
                fo = 1;
            } else {
                // Placeholder path (<DRAFT_ROOT>/…, <SANDBOX>/…) or legacy
                // escaped path (D\c/…) — unescapeColons handles both.
                src = detail::unescapeColons(parts[2]);
            }
            if (parts.size() < static_cast<size_t>(19 + fo)) continue;
            src = detail::deserializePath(src, draftRoot, sandboxRoot);
            Clip::MediaType mt  = static_cast<Clip::MediaType>(std::stoi(parts[3 + fo]));
            int64_t srcDur      = std::stoll(parts[4 + fo]);
            int64_t trimIn      = std::stoll(parts[5 + fo]);
            int64_t trimOut     = std::stoll(parts[6 + fo]);
            int64_t tlIn        = std::stoll(parts[7 + fo]);
            float   speed       = std::stof(parts[8 + fo]);
            // parts[9+fo] = volume (stored in keyframes, we set static fallback)
            bool    rev         = (parts[10 + fo] == "1");
            // parts[11+fo]=scale, parts[12..14+fo]=rotation/tx/ty
            std::string inT     = detail::unescapeColons(parts[15 + fo]);
            int64_t inTDur      = std::stoll(parts[16 + fo]);
            std::string outT    = detail::unescapeColons(parts[17 + fo]);
            int64_t outTDur     = std::stoll(parts[18 + fo]);

            auto clip = std::make_shared<Clip>(id, src, mt);
            clip->setSourceDuration(srcDur);
            clip->setTrimIn(trimIn);
            clip->setTrimOut(trimOut);
            clip->setTimelineIn(tlIn);
            clip->setSpeed(speed);
            clip->setReversed(rev);
            if (!inT.empty())  clip->setInTransitionByName(inT, inTDur);
            if (!outT.empty()) clip->setOutTransitionByName(outT, outTDur);
            currentTrack->addClip(clip);

        } else if (tag == "K" && timeline && currentTrack) {
            if (parts.size() < 5) continue;
            std::string clipId = detail::unescapeColons(parts[1]);
            std::string param  = detail::unescapeColons(parts[2]);
            int64_t timeNs     = std::stoll(parts[3]);
            float   value      = std::stof(parts[4]);
            // v2: optional 6th field = easing type (int)
            InterpolationType easing = InterpolationType::LINEAR;
            if (parts.size() >= 6) easing = static_cast<InterpolationType>(std::stoi(parts[5]));
            auto clip = currentTrack->getClip(clipId);
            if (clip) {
                if (easing == InterpolationType::BEZIER && parts.size() >= 10) {
                    float cp1x = std::stof(parts[6]);
                    float cp1y = std::stof(parts[7]);
                    float cp2x = std::stof(parts[8]);
                    float cp2y = std::stof(parts[9]);
                    clip->addKeyframe(param, timeNs, value, cp1x, cp1y, cp2x, cp2y);
                } else {
                    clip->addKeyframe(param, timeNs, value, easing);
                }
            }

        } else if (tag == "SUB" && timeline && currentTrack) {
            // SUB:<id>:<text>:<tlIn>:<srcDur>:<x>:<y>:<fontSize>:<textColor>:<bgColor>:<align>
            if (parts.size() < 11) continue;
            std::string id   = detail::unescapeColons(parts[1]);
            std::string text = detail::unescapeColons(parts[2]);
            int64_t tlIn     = std::stoll(parts[3]);
            int64_t srcDur   = std::stoll(parts[4]);
            auto sub = std::make_shared<SubtitleClip>(id, text);
            sub->setTimelineIn(tlIn);
            sub->setSourceDuration(srcDur);
            sub->style.x          = std::stof(parts[5]);
            sub->style.y          = std::stof(parts[6]);
            sub->style.fontSizePx = std::stof(parts[7]);
            sub->style.textColor  = static_cast<uint32_t>(std::stoul(parts[8]));
            sub->style.bgColor    = static_cast<uint32_t>(std::stoul(parts[9]));
            sub->style.alignment  = std::stoi(parts[10]);
            currentTrack->addClip(sub);

        } else if (tag == "STICKER" && timeline && currentTrack) {
            // STICKER:<id>:<srcPath>:<srcDur>:<tlIn>:<cx>:<cy>:<scale>:<rot>
            // Path is verbatim; detect Windows drive-letter split same as C: above.
            std::string id = detail::unescapeColons(parts[1]);
            std::string src;
            int fo = 0;
            if (parts.size() > 3
                && parts[2].size() == 1
                && std::isalpha((unsigned char)parts[2][0])
                && !parts[3].empty() && parts[3][0] == '/') {
                src = std::string(1, parts[2][0]) + ":" + parts[3];
                fo = 1;
            } else {
                src = detail::unescapeColons(parts[2]);
            }
            if (parts.size() < static_cast<size_t>(9 + fo)) continue;
            src = detail::deserializePath(src, draftRoot, sandboxRoot);
            int64_t srcDur  = std::stoll(parts[3 + fo]);
            int64_t tlIn    = std::stoll(parts[4 + fo]);
            auto stk = std::make_shared<StickerClip>(id, src);
            stk->setSourceDuration(srcDur);
            stk->setTimelineIn(tlIn);
            stk->centerX         = std::stof(parts[5 + fo]);
            stk->centerY         = std::stof(parts[6 + fo]);
            stk->stickerScale    = std::stof(parts[7 + fo]);
            stk->stickerRotation = std::stof(parts[8 + fo]);
            currentTrack->addClip(stk);
        }
    }

    return timeline;
}

inline bool saveDraft(const Timeline& tl, const std::string& filePath, const std::string& sandboxPath = "") {
    std::string draftRoot = detail::getDirectoryPath(filePath);
    std::string sandboxRoot = sandboxPath;
    if (sandboxRoot.empty()) {
        sandboxRoot = detail::detectSandboxPath(filePath);
    }
    std::ofstream f(filePath, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;
    f << serializeTimeline(tl, draftRoot, sandboxRoot);
    return f.good();
}

inline std::shared_ptr<Timeline> loadDraft(const std::string& filePath, const std::string& sandboxPath = "") {
    std::ifstream f(filePath);
    if (!f.is_open()) return nullptr;
    std::ostringstream buf;
    buf << f.rdbuf();
    
    std::string draftRoot = detail::getDirectoryPath(filePath);
    std::string sandboxRoot = sandboxPath;
    if (sandboxRoot.empty()) {
        sandboxRoot = detail::detectSandboxPath(filePath);
    }
    return deserializeTimeline(buf.str(), draftRoot, sandboxRoot);
}

} // namespace timeline
} // namespace video
} // namespace sdk
