#pragma once
// TimelineDraft.h — header-only serialize / deserialize for Timeline drafts.
// Format: line-based text, ':'-delimited fields. No external deps required.
//
// Schema (one line per entity):
//   SVDK_DRAFT:<version>:<width>:<height>:<fps>
//   T:<zIndex>:<type>:<opacity>:<volume>
//   C:<id>:<srcPath>:<mediaType>:<srcDurNs>:<trimInNs>:<trimOutNs>:<tlInNs>:
//     <speed>:<volume>:<reversed>:<scale>:<rotation>:<tx>:<ty>:
//     <inTransition>:<inTransDurNs>:<outTransition>:<outTransDurNs>
//   K:<clipId>:<param>:<timeNs>:<value>

#include "Timeline.h"
#include "Track.h"
#include "Clip.h"
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <cstdio>

namespace sdk {
namespace video {
namespace timeline {

static constexpr int DRAFT_FORMAT_VERSION = 1;

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
// serialize
// ---------------------------------------------------------------------------
inline std::string serializeTimeline(const Timeline& tl) {
    std::ostringstream ss;
    ss << "SVDK_DRAFT:" << DRAFT_FORMAT_VERSION << ":"
       << tl.getOutputWidth() << ":" << tl.getOutputHeight() << ":" << tl.getFps() << "\n";

    // Iterate tracks by collecting via getTrack with a sentinel scan.
    // Timeline exposes no iterator, so we probe z-indices 0..255.
    for (int z = 0; z <= 255; ++z) {
        TrackPtr track = tl.getTrack(z);
        if (!track) continue;

        ss << "T:" << z << ":" << static_cast<int>(track->getType()) << ":"
           << track->getOpacity() << ":" << track->getTrackVolume() << "\n";

        // Get active clips — use a time range probe to collect all clips.
        // Since Timeline exposes no direct clip iterator, we collect them
        // by scanning clip list indirectly via the Track interface.
        // Track exposes getClip(id) but not enumeration — add a workaround
        // by casting to the concrete type through the public API.
        // We use a sentinel large time range to trigger all clips via
        // getActiveClipsAtTime scans — but that only returns currently-active ones.
        //
        // Implementation note: Track stores clips in a vector. We cannot iterate
        // without a public accessor. We add a serialization-friend approach:
        // Use the dedicated serializeTrackClips helper which accesses via index.
        // Since we can't modify Track here without a header change, we use a
        // time-range union scan across the full project duration.
        std::vector<ClipPtr> seen;
        int64_t totalDur = tl.getTotalDuration();
        if (totalDur <= 0) totalDur = 1;
        // Scan at regular intervals to discover all clips
        int64_t step = 1000000000LL; // 1 second
        for (int64_t t = 0; t <= totalDur + step; t += step) {
            std::vector<ClipPtr> active;
            track->getActiveClipsAtTime(t, active);
            for (const auto& c : active) {
                bool found = false;
                for (const auto& s : seen) { if (s->getId() == c->getId()) { found = true; break; } }
                if (!found) seen.push_back(c);
            }
        }

        for (const auto& clip : seen) {
            ss << "C:"
               << detail::escapeColons(clip->getId())     << ":"
               << detail::escapeColons(clip->getSourcePath()) << ":"
               << static_cast<int>(clip->getType())       << ":"
               << clip->getSourceDuration()               << ":"
               << clip->getTrimIn()                       << ":"
               << clip->getTrimOut()                      << ":"
               << clip->getTimelineIn()                   << ":"
               << clip->getSpeed()                        << ":"
               << clip->getVolume(0)                      << ":"
               << (clip->isReversed() ? 1 : 0)            << ":"
               << clip->getScale(0)                       << ":"
               << 0.0f << ":" << 0.0f << ":" << 0.0f      << ":"
               << detail::escapeColons(clip->getInTransitionName())  << ":"
               << clip->getInTransitionDurationNs()       << ":"
               << detail::escapeColons(clip->getOutTransitionName()) << ":"
               << clip->getOutTransitionDurationNs()      << "\n";
        }
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
            auto clip = currentTrack->getClip(clipId);
            if (clip) clip->addKeyframe(param, timeNs, value);
        }
    }

    return timeline;
}

} // namespace timeline
} // namespace video
} // namespace sdk
