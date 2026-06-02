#include <iostream>
#include <cassert>
#include <memory>
#include <vector>
#include <string>
#include "../core/include/timeline/Track.h"
#include "../core/include/timeline/Clip.h"

using namespace sdk::video::timeline;

void test_track_stable_sort_same_timeline_in() {
    Track track(0, Track::TrackType::MAIN_VIDEO);

    // Add three clips at the same starting time
    auto clip1 = std::make_shared<Clip>("clip_1", "v1.mp4", Clip::MediaType::VIDEO);
    clip1->setTimelineIn(0);
    clip1->setSourceDuration(1000000000);
    clip1->setTrimOut(1000000000);

    auto clip2 = std::make_shared<Clip>("clip_2", "v2.mp4", Clip::MediaType::VIDEO);
    clip2->setTimelineIn(0);
    clip2->setSourceDuration(1000000000);
    clip2->setTrimOut(1000000000);

    auto clip3 = std::make_shared<Clip>("clip_3", "v3.mp4", Clip::MediaType::VIDEO);
    clip3->setTimelineIn(0);
    clip3->setSourceDuration(1000000000);
    clip3->setTrimOut(1000000000);

    track.addClip(clip1);
    track.addClip(clip2);
    track.addClip(clip3);

    // If we have multiple clips at the same time, we expect getActiveClipAtTime
    // to return the "last added" one if we want "top-most" behavior on a single track
    // Or at least it should be predictable.
    // Currently, it returns the first one it finds.

    auto active = track.getActiveClipAtTime(500000000); // 0.5s
    assert(active != nullptr);
    std::cout << "Active clip at 0.5s: " << active->getId() << std::endl;

    // After fixing, we expect clip_3 (last added) to be returned if they overlap exactly.
    // This is because in Compositor, we usually want the latest added clip to take precedence
    // if they are on the same track (though ideally tracks shouldn't have overlaps except for transitions).
    assert(active->getId() == "clip_3");

    std::cout << "test_track_stable_sort_same_timeline_in passed" << std::endl;
}

void test_track_overlapping_selection() {
    Track track(0, Track::TrackType::MAIN_VIDEO);

    // Clip A: 0s - 2s
    auto clipA = std::make_shared<Clip>("clip_A", "a.mp4", Clip::MediaType::VIDEO);
    clipA->setTimelineIn(0);
    clipA->setSourceDuration(2000000000);
    clipA->setTrimOut(2000000000);
    track.addClip(clipA);

    // Clip B: 1s - 3s
    auto clipB = std::make_shared<Clip>("clip_B", "b.mp4", Clip::MediaType::VIDEO);
    clipB->setTimelineIn(1000000000);
    clipB->setSourceDuration(2000000000);
    clipB->setTrimOut(2000000000);
    track.addClip(clipB);

    // At 1.5s, both are active.
    // getActiveClipAtTime should return Clip B because it started later (often used for transition "to" clip)
    // or simply because it was added later/starts later.
    auto active = track.getActiveClipAtTime(1500000000);
    assert(active != nullptr);
    assert(active->getId() == "clip_B");

    std::cout << "test_track_overlapping_selection passed" << std::endl;
}

int main() {
    try {
        test_track_stable_sort_same_timeline_in();
    } catch (const std::exception& e) {
        std::cerr << "test_track_stable_sort_same_timeline_in failed: " << e.what() << std::endl;
    }

    try {
        test_track_overlapping_selection();
    } catch (const std::exception& e) {
        std::cerr << "test_track_overlapping_selection failed: " << e.what() << std::endl;
    }

    return 0;
}
