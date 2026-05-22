#include <iostream>
#include <cassert>
#include <memory>
#include <vector>
#include <cmath>
#include "../core/include/timeline/Timeline.h"
#include "../core/include/timeline/TimelineDraft.h"
#include "../core/include/timeline/Track.h"
#include "../core/include/timeline/Clip.h"

using namespace sdk::video::timeline;

// Use a small epsilon for float comparisons
const float EPSILON = 1e-5f;

void test_timeline_basic_properties() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    assert(timeline->getOutputWidth() == 1920);
    assert(timeline->getOutputHeight() == 1080);
    assert(timeline->getFps() == 30);
    std::cout << "test_timeline_basic_properties passed" << std::endl;
}

void test_sequential_clips_and_gaps() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    auto track = timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);

    // Clip 1: 0s to 2s
    auto clip1 = std::make_shared<Clip>("clip_1", "v1.mp4", Clip::MediaType::VIDEO);
    clip1->setSourceDuration(10000000000); // 10s
    clip1->setTimelineIn(0);
    clip1->setTrimIn(0);
    clip1->setTrimOut(2000000000); // 2s
    track->addClip(clip1);

    // Gap: 2s to 3s

    // Clip 2: 3s to 5s
    auto clip2 = std::make_shared<Clip>("clip_2", "v2.mp4", Clip::MediaType::VIDEO);
    clip2->setSourceDuration(10000000000); // 10s
    clip2->setTimelineIn(3000000000); // 3s
    clip2->setTrimIn(0);
    clip2->setTrimOut(2000000000); // 2s
    track->addClip(clip2);

    // Assertions
    assert(track->getActiveClipAtTime(1000000000)->getId() == "clip_1"); // 1s
    assert(track->getActiveClipAtTime(2000000000) == nullptr);           // 2s (End is exclusive)
    assert(track->getActiveClipAtTime(2500000000) == nullptr);           // 2.5s (Gap)
    assert(track->getActiveClipAtTime(3000000000)->getId() == "clip_2"); // 3s
    assert(track->getActiveClipAtTime(4000000000)->getId() == "clip_2"); // 4s
    assert(track->getActiveClipAtTime(5000000000) == nullptr);           // 5s

    std::cout << "test_sequential_clips_and_gaps passed" << std::endl;
}

void test_multi_track_activation() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    auto mainTrack = timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);
    auto pipTrack = timeline->addTrack(10, Track::TrackType::PIP_VIDEO);

    // Main clip: 0s to 5s
    auto mainClip = std::make_shared<Clip>("main", "main.mp4", Clip::MediaType::VIDEO);
    mainClip->setSourceDuration(10000000000); // 10s
    mainClip->setTimelineIn(0);
    mainClip->setTrimOut(5000000000);
    mainTrack->addClip(mainClip);

    // PIP clip: 1s to 3s
    auto pipClip = std::make_shared<Clip>("pip", "pip.mp4", Clip::MediaType::VIDEO);
    pipClip->setSourceDuration(10000000000); // 10s
    pipClip->setTimelineIn(1000000000);
    pipClip->setTrimOut(2000000000);
    pipTrack->addClip(pipClip);

    std::vector<ClipPtr> activeClips;

    // At 0.5s: only main
    timeline->getActiveVideoClipsAtTime(500000000, activeClips);
    assert(activeClips.size() == 1);
    assert(activeClips[0]->getId() == "main");

    // At 2s: both main and pip
    timeline->getActiveVideoClipsAtTime(2000000000, activeClips);
    assert(activeClips.size() == 2);
    assert(activeClips[0]->getId() == "main");
    assert(activeClips[1]->getId() == "pip");

    // At 4s: only main
    timeline->getActiveVideoClipsAtTime(4000000000, activeClips);
    assert(activeClips.size() == 1);
    assert(activeClips[0]->getId() == "main");

    std::cout << "test_multi_track_activation passed" << std::endl;
}

void test_transition_properties() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);

    // Default should be NONE
    assert(clip->getInTransitionType() == TransitionType::NONE);
    assert(clip->getInTransitionDurationNs() == 0);

    clip->setInTransition(TransitionType::WIPE_LEFT, 500000000); // 0.5s
    assert(clip->getInTransitionType() == TransitionType::WIPE_LEFT);
    assert(clip->getInTransitionDurationNs() == 500000000);

    std::cout << "test_transition_properties passed" << std::endl;
}

void test_out_transition_properties() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);

    // Default should be NONE
    assert(clip->getOutTransitionType() == TransitionType::NONE);
    assert(clip->getOutTransitionDurationNs() == 0);

    clip->setOutTransition(TransitionType::CROSSFADE, 300000000); // 0.3s
    assert(clip->getOutTransitionType() == TransitionType::CROSSFADE);
    assert(clip->getOutTransitionDurationNs() == 300000000);

    std::cout << "test_out_transition_properties passed" << std::endl;
}

void test_default_trim_out() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);
    clip->setSourceDuration(5000000000); // 5s
    clip->setTimelineIn(0);
    clip->setTrimIn(1000000000); // 1s
    clip->setTrimOut(0); // Default

    // Effective TrimOut should be SourceDuration (5s)
    // Duration = 5s - 1s = 4s
    assert(clip->getEffectiveTrimOut() == 5000000000);
    assert(clip->getTimelineOut() == 4000000000);

    std::cout << "test_default_trim_out passed" << std::endl;
}

void test_overlapping_clips_retrieval() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    auto track = timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);

    // Clip 1: 0s to 2s
    auto clip1 = std::make_shared<Clip>("clip_1", "v1.mp4", Clip::MediaType::VIDEO);
    clip1->setSourceDuration(10000000000);
    clip1->setTimelineIn(0);
    clip1->setTrimIn(0);
    clip1->setTrimOut(2000000000);
    track->addClip(clip1);

    // Clip 2: 1s to 3s (Overlaps with Clip 1)
    auto clip2 = std::make_shared<Clip>("clip_2", "v2.mp4", Clip::MediaType::VIDEO);
    clip2->setSourceDuration(10000000000);
    clip2->setTimelineIn(1000000000);
    clip2->setTrimIn(0);
    clip2->setTrimOut(2000000000);
    track->addClip(clip2);

    std::vector<ClipPtr> activeClips;

    // At 0.5s: only clip 1
    track->getActiveClipsAtTime(500000000, activeClips);
    assert(activeClips.size() == 1);
    assert(activeClips[0]->getId() == "clip_1");
    activeClips.clear();

    // At 1.5s: both clip 1 and clip 2
    track->getActiveClipsAtTime(1500000000, activeClips);
    assert(activeClips.size() == 2);
    // Order should be by TimelineIn (Clip 1 then Clip 2)
    assert(activeClips[0]->getId() == "clip_1");
    assert(activeClips[1]->getId() == "clip_2");
    activeClips.clear();

    // At 2.5s: only clip 2
    track->getActiveClipsAtTime(2500000000, activeClips);
    assert(activeClips.size() == 1);
    assert(activeClips[0]->getId() == "clip_2");
    activeClips.clear();

    std::cout << "test_overlapping_clips_retrieval passed" << std::endl;
}

void test_duration_and_speed() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    auto track = timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);

    // Clip: 0s to 4s in timeline (but 2x speed, so 8s source used)
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);
    clip->setSourceDuration(10000000000); // 10s
    clip->setTimelineIn(0);
    clip->setTrimIn(0);
    clip->setTrimOut(8000000000); // 8s
    clip->setSpeed(2.0f);
    track->addClip(clip);

    // Timeline duration should be 8s / 2.0 = 4s
    assert(clip->getTimelineOut() == 4000000000);
    assert(timeline->getTotalDuration() == 4000000000);

    // Add another clip at 5s
    auto clip2 = std::make_shared<Clip>("clip2", "v2.mp4", Clip::MediaType::VIDEO);
    clip2->setSourceDuration(10000000000); // 10s
    clip2->setTimelineIn(5000000000); // 5s
    clip2->setTrimIn(0);
    clip2->setTrimOut(1000000000); // 1s
    clip2->setSpeed(0.5f); // 0.5x speed
    track->addClip(clip2);

    // Clip2 timeline duration = 1s / 0.5 = 2s. TimelineOut = 5s + 2s = 7s.
    assert(clip2->getTimelineOut() == 7000000000);
    assert(timeline->getTotalDuration() == 7000000000);

    std::cout << "test_duration_and_speed passed" << std::endl;
}

void test_keyframe_interpolation() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);

    // Default opacity should be 1.0
    assert(std::abs(clip->getOpacity(0) - 1.0f) < EPSILON);

    // Add keyframes for opacity: 0.0 at 0s, 1.0 at 1s, 0.5 at 2s
    clip->addKeyframe("opacity", 0, 0.0f);
    clip->addKeyframe("opacity", 1000000000, 1.0f);
    clip->addKeyframe("opacity", 2000000000, 0.5f);

    // Test values at keyframes
    assert(std::abs(clip->getOpacity(0) - 0.0f) < EPSILON);
    assert(std::abs(clip->getOpacity(1000000000) - 1.0f) < EPSILON);
    assert(std::abs(clip->getOpacity(2000000000) - 0.5f) < EPSILON);

    // Test interpolated values
    assert(std::abs(clip->getOpacity(500000000) - 0.5f) < EPSILON);  // halfway between 0s and 1s
    assert(std::abs(clip->getOpacity(1500000000) - 0.75f) < EPSILON); // halfway between 1s and 2s

    // Test out of bounds
    assert(std::abs(clip->getOpacity(-1000) - 0.0f) < EPSILON);      // clamped to first
    assert(std::abs(clip->getOpacity(3000000000) - 0.5f) < EPSILON); // clamped to last

    std::cout << "test_keyframe_interpolation passed" << std::endl;
}

void test_clip_boundary_trim() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);
    clip->setSourceDuration(5000000000); // 5s
    clip->setTimelineIn(1000000000); // 1s

    // Case 1: trimIn == trimOut
    clip->setTrimIn(2000000000);
    clip->setTrimOut(2000000000);
    assert(clip->getTimelineOut() == 1000000000); // Duration 0

    // Case 2: trimIn > trimOut
    clip->setTrimIn(3000000000);
    clip->setTrimOut(2000000000);
    assert(clip->getEffectiveTrimIn() == 2000000000);
    assert(clip->getTimelineOut() == 1000000000); // Duration 0

    // Case 3: trimOut > sourceDuration
    clip->setTrimIn(0);
    clip->setTrimOut(10000000000); // 10s, but source is 5s
    assert(clip->getEffectiveTrimOut() == 5000000000);
    assert(clip->getTimelineOut() == 6000000000); // 1s + 5s = 6s

    // Case 4: negative trimIn
    clip->setTrimIn(-1000);
    assert(clip->getEffectiveTrimIn() == 0);

    std::cout << "test_clip_boundary_trim passed" << std::endl;
}

void test_clip_speed_limits() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);
    clip->setSourceDuration(5000000000); // 5s
    clip->setTimelineIn(0);
    clip->setTrimOut(1000000000); // 1s

    // Speed 0
    clip->setSpeed(0.0f);
    assert(clip->getTimelineOut() == 0);

    // Speed negative
    clip->setSpeed(-1.0f);
    assert(clip->getTimelineOut() == 0);

    // Very small speed (above threshold 0.00001f)
    clip->setSpeed(0.1f);
    // 1s / 0.1 = 10s = 10,000,000,000 ns
    int64_t expectedOut = 10000000000LL;
    assert(std::abs(clip->getTimelineOut() - expectedOut) < 1000);

    // Below threshold
    clip->setSpeed(0.000001f);
    assert(clip->getTimelineOut() == 0);

    std::cout << "test_clip_speed_limits passed" << std::endl;
}

void test_transition_boundary() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);
    clip->setSourceDuration(5000000000); // 5s
    clip->setTimelineIn(0);
    clip->setTrimOut(1000000000); // 1s

    // Transition duration (2s) > Clip duration (1s)
    // This is a semantic boundary. The engine should probably clamp it during rendering,
    // but here we just check if it allows setting it.
    clip->setInTransition(TransitionType::CROSSFADE, 2000000000);
    assert(clip->getInTransitionDurationNs() == 2000000000);

    std::cout << "test_transition_boundary passed" << std::endl;
}

void test_sparse_keyframes() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);

    // Case 1: No keyframes, return default
    assert(std::abs(clip->getOpacity(100) - 1.0f) < EPSILON);

    // Case 2: Single keyframe
    clip->addKeyframe("opacity", 1000, 0.5f);
    assert(std::abs(clip->getOpacity(0) - 0.5f) < EPSILON);
    assert(std::abs(clip->getOpacity(1000) - 0.5f) < EPSILON);
    assert(std::abs(clip->getOpacity(2000) - 0.5f) < EPSILON);

    std::cout << "test_sparse_keyframes passed" << std::endl;
}

void test_timeline_time_semantics() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    auto track = timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);

    auto clip = std::make_shared<Clip>("c1", "v1.mp4", Clip::MediaType::VIDEO);
    clip->setSourceDuration(1000000000);
    clip->setTimelineIn(1000000000); // starts at 1s
    clip->setTrimOut(1000000000);    // duration 1s, ends at 2s
    track->addClip(clip);

    // Start inclusive: at 1s, clip should be active
    assert(track->getActiveClipAtTime(1000000000) != nullptr);
    assert(track->getActiveClipAtTime(1000000000)->getId() == "c1");

    // End exclusive: at 2s, clip should NOT be active
    assert(track->getActiveClipAtTime(2000000000) == nullptr);

    // Just before end
    assert(track->getActiveClipAtTime(1999999999) != nullptr);

    std::cout << "test_timeline_time_semantics passed" << std::endl;
}

void test_bezier_interpolation() {
    auto clip = std::make_shared<Clip>("clip", "v.mp4", Clip::MediaType::VIDEO);

    // 1. BEZIER(0,0,1,1) should be LINEAR
    clip->addKeyframe("opacity", 0, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    clip->addKeyframe("opacity", 1000000000, 1.0f);

    assert(std::abs(clip->getOpacity(500000000) - 0.5f) < 0.01f);
    assert(std::abs(clip->getOpacity(250000000) - 0.25f) < 0.01f);

    // 2. BEZIER(0.42, 0, 0.58, 1) should be EASE_IN_OUT
    clip->clearKeyframes("opacity");
    clip->addKeyframe("opacity", 0, 0.0f, 0.42f, 0.0f, 0.58f, 1.0f);
    clip->addKeyframe("opacity", 1000000000, 1.0f);

    // At 0.5, ease-in-out should be 0.5
    assert(std::abs(clip->getOpacity(500000000) - 0.5f) < 0.01f);
    // At 0.2, ease-in-out should be LESS than 0.2 (slower start)
    assert(clip->getOpacity(200000000) < 0.2f);
    // At 0.8, ease-in-out should be MORE than 0.8 (slower end)
    assert(clip->getOpacity(800000000) > 0.8f);

    std::cout << "test_bezier_interpolation passed" << std::endl;
}

void test_bezier_serialization() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    auto track = timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);
    auto clip = std::make_shared<Clip>("c1", "v1.mp4", Clip::MediaType::VIDEO);
    clip->addKeyframe("opacity", 0, 0.0f, 0.1f, 0.2f, 0.3f, 0.4f);
    clip->addKeyframe("opacity", 1000000000, 1.0f);
    track->addClip(clip);

    std::string data = serializeTimeline(*timeline);

    // Check if the K: line contains our bezier points
    assert(data.find(":0.1:0.2:0.3:0.4") != std::string::npos);

    // Round trip
    std::string tempFile = "test_bezier.svdk";
    saveDraft(*timeline, tempFile);
    auto loadedTl = loadDraft(tempFile);
    remove(tempFile.c_str());

    assert(loadedTl != nullptr);
    auto loadedClip = loadedTl->getTrack(0)->getClip("c1");
    auto kfs = loadedClip->getKeyframes("opacity");
    assert(kfs.size() == 2);
    assert(kfs[0].second.easing == InterpolationType::BEZIER);
    assert(std::abs(kfs[0].second.cp1x - 0.1f) < EPSILON);
    assert(std::abs(kfs[0].second.cp1y - 0.2f) < EPSILON);
    assert(std::abs(kfs[0].second.cp2x - 0.3f) < EPSILON);
    assert(std::abs(kfs[0].second.cp2y - 0.4f) < EPSILON);

    std::cout << "test_bezier_serialization passed" << std::endl;
}

void test_effect_clip() {
    auto effect = std::make_shared<EffectClip>("fx1", "blur");
    assert(effect->getType() == Clip::MediaType::EFFECT);
    assert(effect->getEffectType() == "blur");

    // Static intensity
    effect->setIntensity(0.8f);
    assert(std::abs(effect->getIntensity(0) - 0.8f) < EPSILON);

    // Keyframed intensity
    effect->addKeyframe("intensity", 0, 0.0f);
    effect->addKeyframe("intensity", 1000000000, 1.0f);
    assert(std::abs(effect->getIntensity(500000000) - 0.5f) < EPSILON);

    std::cout << "test_effect_clip passed" << std::endl;
}

void test_time_remapping() {
    auto clip = std::make_shared<Clip>("c1", "v1.mp4", Clip::MediaType::VIDEO);

    // 1. Linear constant speed (0.5x)
    clip->setSpeedCurvePoint(0, 0.5f);
    assert(std::abs(clip->getSpeedAtTime(500000000) - 0.5f) < EPSILON);
    assert(clip->getRemappedTime(1000000000) == 500000000);

    // 2. Variable speed: 0s(0.5x) -> 2s(2.0x)
    // At 1s, speed should be 1.25x
    clip->setSpeedCurvePoint(2000000000, 2.0f);
    assert(std::abs(clip->getSpeedAtTime(1000000000) - 1.25f) < EPSILON);

    // Integral from 0 to 1s:
    // v(t) = 0.5 + (2.0 - 0.5) * t / 2.0 = 0.5 + 0.75 * t
    // SourceTime(t) = 0.5t + 0.375 * t^2
    // SourceTime(1s) = 0.5 * 1 + 0.375 * 1^2 = 0.875s = 875,000,000 ns
    assert(clip->getRemappedTime(1000000000) == 875000000);

    std::cout << "test_time_remapping passed" << std::endl;
}

void test_draft_path_resilience() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    auto track = timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);

    std::string draftRoot = "E:/projects/drafts/my_project/";
    std::string sandboxRoot = "C:/Users/app/AppData/Local/Temp/sandbox/";

    auto clip1 = std::make_shared<Clip>("clip1", "E:\\projects\\drafts\\my_project\\assets\\video.mp4", Clip::MediaType::VIDEO);
    auto clip2 = std::make_shared<Clip>("clip2", "C:/Users/app/AppData/Local/Temp/sandbox/files/audio.wav", Clip::MediaType::AUDIO);
    auto clip3 = std::make_shared<Clip>("clip3", "D:/movies/intro.mp4", Clip::MediaType::VIDEO);

    track->addClip(clip1);
    track->addClip(clip2);
    track->addClip(clip3);

    // Serialize
    std::string serialized = serializeTimeline(*timeline, draftRoot, sandboxRoot);

    // Verify placeholders are present
    assert(serialized.find("<DRAFT_ROOT>/assets/video.mp4") != std::string::npos);
    assert(serialized.find("<SANDBOX>/files/audio.wav") != std::string::npos);
    assert(serialized.find("D:/movies/intro.mp4") != std::string::npos);

    // Now deserialize in a different environment/paths (mimicking app reinstall/transfer)
    std::string newDraftRoot = "F:/new_projects/drafts/my_project/";
    std::string newSandboxRoot = "D:/sandbox_root/";

    auto deserializedTimeline = deserializeTimeline(serialized, newDraftRoot, newSandboxRoot);
    assert(deserializedTimeline != nullptr);

    auto loadedTrack = deserializedTimeline->getTrack(0);
    assert(loadedTrack != nullptr);

    auto loadedClip1 = loadedTrack->getClip("clip1");
    auto loadedClip2 = loadedTrack->getClip("clip2");
    auto loadedClip3 = loadedTrack->getClip("clip3");

    assert(loadedClip1 != nullptr);
    assert(loadedClip2 != nullptr);
    assert(loadedClip3 != nullptr);

    // Verify paths are resolved to the new roots
    assert(loadedClip1->getSourcePath() == "F:/new_projects/drafts/my_project/assets/video.mp4");
    assert(loadedClip2->getSourcePath() == "D:/sandbox_root/files/audio.wav");
    assert(loadedClip3->getSourcePath() == "D:/movies/intro.mp4");

    std::cout << "test_draft_path_resilience passed" << std::endl;
}

int main() {
    test_timeline_basic_properties();
    test_clip_boundary_trim();
    test_clip_speed_limits();
    test_transition_boundary();
    test_sparse_keyframes();
    test_timeline_time_semantics();
    test_sequential_clips_and_gaps();
    test_multi_track_activation();
    test_transition_properties();
    test_out_transition_properties();
    test_default_trim_out();
    test_overlapping_clips_retrieval();
    test_duration_and_speed();
    test_keyframe_interpolation();
    test_bezier_interpolation();
    test_bezier_serialization();
    test_effect_clip();
    test_time_remapping();
    test_draft_path_resilience();
    return 0;
}
