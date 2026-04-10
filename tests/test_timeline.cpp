#include <iostream>
#include <cassert>
#include <memory>
#include "../core/include/timeline/Timeline.h"
#include "../core/include/timeline/Track.h"
#include "../core/include/timeline/Clip.h"

using namespace sdk::video::timeline;

void test_timeline_creation() {
    auto timeline = std::make_shared<Timeline>(1920, 1080, 30);
    assert(timeline->getOutputWidth() == 1920);
    assert(timeline->getOutputHeight() == 1080);

    timeline->addTrack(0, Track::TrackType::MAIN_VIDEO);
    auto track = timeline->getTrack(0);
    assert(track != nullptr);

    auto clip = std::make_shared<Clip>("clip_1", "path/to/video.mp4", Clip::MediaType::VIDEO);
    clip->setTimelineIn(0);
    clip->setTrimOut(5000000); // 5 seconds
    track->addClip(clip);

    auto activeClip = track->getActiveClipAtTime(1000000); // 1 second
    assert(activeClip != nullptr);
    assert(activeClip->getId() == "clip_1");

    std::cout << "test_timeline_creation passed" << std::endl;
}

int main() {
    test_timeline_creation();
    return 0;
}
