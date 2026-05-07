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
#include <cstdio>

namespace sdk {
namespace video {
namespace timeline {

static constexpr int DRAFT_FORMAT_VERSION = 2;

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

} // namespace detail

// ---------------------------------------------------------------------------
// serialize helpers
// ---------------------------------------------------------------------------
namespace detail {

inline void serializeClip(std::ostringstream& ss, const ClipPtr& clip) {
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
        // STICKER:<id>:<srcPath>:<srcDur>:<tlIn>:<cx>:<cy>:<scale>:<rot>
        ss << "STICKER:"
           << escapeColons(clipId)                   << ":"
           << escapeColons(stk->getSourcePath())     << ":"
           << stk->getSourceDuration()               << ":"
           << stk->getTimelineIn()                   << ":"
           << stk->centerX                           << ":"
           << stk->centerY                           << ":"
           << stk->stickerScale                      << ":"
           << stk->stickerRotation                   << "\n";
        return;
    }
    // ── Generic Clip ─────────────────────────────────────────────────────────
    ss << "C:"
       << escapeColons(clipId)                          << ":"
       << escapeColons(clip->getSourcePath())           << ":"
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
       << clip->getInTransitionDurationNs()             << ":"
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
               << static_cast<int>(entry.easing) << "\n";
        }
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// serialize
// ---------------------------------------------------------------------------
inline std::string serializeTimeline(const Timeline& tl) {
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
            detail::serializeClip(ss, clip);
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// saveDraft / loadDraft  (file I/O wrappers)
// ---------------------------------------------------------------------------
inline bool saveDraft(const Timeline& tl, const std::string& filePath) {
    std::ofstream f(filePath, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;
    f << serializeTimeline(tl);
    return f.good();
}

inline std::shared_ptr<Timeline> loadDraft(const std::string& filePath) {
    std::ifstream f(filePath);
    if (!f.is_open()) return nullptr;
    std::ostringstream buf;
    buf << f.rdbuf();
    const std::string content = buf.str();

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
            if (parts.size() < 19) continue;
            std::string id      = detail::unescapeColons(parts[1]);
            std::string src     = detail::unescapeColons(parts[2]);
            Clip::MediaType mt  = static_cast<Clip::MediaType>(std::stoi(parts[3]));
            int64_t srcDur      = std::stoll(parts[4]);
            int64_t trimIn      = std::stoll(parts[5]);
            int64_t trimOut     = std::stoll(parts[6]);
            int64_t tlIn        = std::stoll(parts[7]);
            float   speed       = std::stof(parts[8]);
            // parts[9]  = volume (stored in keyframes, we set static fallback)
            bool    rev         = (parts[10] == "1");
            // parts[11] = scale, parts[12..14] = rotation/tx/ty (not settable via public API here)
            std::string inT     = detail::unescapeColons(parts[15]);
            int64_t inTDur      = std::stoll(parts[16]);
            std::string outT    = detail::unescapeColons(parts[17]);
            int64_t outTDur     = std::stoll(parts[18]);

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
            if (clip) clip->addKeyframe(param, timeNs, value, easing);

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
            if (parts.size() < 9) continue;
            std::string id  = detail::unescapeColons(parts[1]);
            std::string src = detail::unescapeColons(parts[2]);
            int64_t srcDur  = std::stoll(parts[3]);
            int64_t tlIn    = std::stoll(parts[4]);
            auto stk = std::make_shared<StickerClip>(id, src);
            stk->setSourceDuration(srcDur);
            stk->setTimelineIn(tlIn);
            stk->centerX         = std::stof(parts[5]);
            stk->centerY         = std::stof(parts[6]);
            stk->stickerScale    = std::stof(parts[7]);
            stk->stickerRotation = std::stof(parts[8]);
            currentTrack->addClip(stk);
        }
    }

    return timeline;
}

} // namespace timeline
} // namespace video
} // namespace sdk
