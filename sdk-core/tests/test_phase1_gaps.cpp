/**
 * test_phase1_gaps.cpp
 *
 * Phase 1 功能差距补齐单元测试，覆盖：
 *   1.  HandLandmarkDetector — 构造、stub loadModel、getLatestResult
 *   2.  HandResult 手势辅助方法 (isPinching / isVictoryGesture)
 *   3.  Clip::MediaType::TEXT / STICKER 新枚举值
 *   4.  SubtitleClip 使用 MediaType::TEXT
 *   5.  StickerClip 使用 MediaType::STICKER
 *   6.  SegmentationFilter::Mode 枚举可访问
 *   7.  AutoSubtitleEngine::createDefault() 返回 stub，recognize() 返回空列表
 *   8.  AutoSubtitleEngine 工厂注册 + 自定义实现
 *   9.  TemplateEngine::loadFromString() 解析 JSON
 *   10. TemplateEngine::apply() 生成 Timeline（slot 数量、轨道类型）
 *   11. TemplateEngine 缺少 filledPath 时 apply() 仍不崩溃
 *   12. VideoTemplate::allSlotsFilled() 逻辑
 */

#include <cassert>
#include <iostream>
#include <string>
#include <memory>
#include <vector>

#include "../core/include/ai/HandLandmarkDetector.h"
#include "../core/include/ai/AutoSubtitleEngine.h"
#include "../core/include/ai/SegmentationFilter.h"
#include "../core/include/timeline/Clip.h"
#include "../core/include/timeline/SubtitleClip.h"
#include "../core/include/timeline/VideoTemplate.h"
#include "../core/include/timeline/TemplateEngine.h"
#include "../core/include/timeline/Track.h"
#include "../core/include/timeline/Timeline.h"

using namespace sdk::video;
using namespace sdk::video::ai;
using namespace sdk::video::timeline;

// ---------------------------------------------------------------------------
static void pass(const std::string& name) {
    std::cout << "\033[32m  [PASS]\033[0m " << name << "\n";
}
static void fail(const std::string& name, const std::string& reason) {
    std::cout << "\033[31m  [FAIL]\033[0m " << name << " — " << reason << "\n";
    std::exit(1);
}
#define ASSERT_TRUE(expr, name) \
    do { if (!(expr)) fail(name, #expr " was false"); else pass(name); } while(0)
#define ASSERT_EQ(a, b, name) \
    do { if ((a) != (b)) fail(name, #a " != " #b); else pass(name); } while(0)

// ---------------------------------------------------------------------------
// Test 1-2: HandLandmarkDetector
// ---------------------------------------------------------------------------
static void testHandLandmarkDetector() {
    std::cout << "\n=== HandLandmarkDetector ===\n";

    // T1: construct without crashing
    HandLandmarkDetector detector;
    ASSERT_TRUE(!detector.isLoaded(), "T1: not loaded before loadModel");

    // T2: stub loadModel (no real model file) — should succeed (stub path)
    bool ok = detector.loadModel("/nonexistent/hand_landmark.tflite");
    ASSERT_TRUE(ok, "T2: loadModel stub returns true");
    ASSERT_TRUE(detector.isLoaded(), "T2: isLoaded after loadModel");

    // T3: getLatestResult returns empty (0 hands) — no crash
    HandFrameResult result = detector.getLatestResult();
    ASSERT_EQ(result.handCount, 0, "T3: initial handCount == 0");

    // T4: runSync with dummy 1x1 pixel
    uint8_t pixel[4] = {128, 64, 32, 255};
    HandFrameResult syncResult = detector.runSync(pixel, 1, 1);
    ASSERT_EQ(syncResult.handCount, 0, "T4: stub runSync returns 0 hands");

    detector.release();
    ASSERT_TRUE(!detector.isLoaded(), "T5: isLoaded false after release");
}

// ---------------------------------------------------------------------------
// Test 6-7: HandResult gesture helpers
// ---------------------------------------------------------------------------
static void testHandResultGestures() {
    std::cout << "\n=== HandResult gesture helpers ===\n";

    HandResult hand;
    hand.detected = true;
    hand.handScore = 0.95f;

    // All y=0.5 — no finger extended
    for (auto& lm : hand.landmarks) { lm.x = 0.5f; lm.y = 0.5f; }

    // isPinching: thumb tip and index tip at same point → should pinch
    hand.landmarks[HandLandmarkIndex::THUMB_TIP].x = 0.5f;
    hand.landmarks[HandLandmarkIndex::THUMB_TIP].y = 0.5f;
    hand.landmarks[HandLandmarkIndex::INDEX_TIP].x = 0.5f;
    hand.landmarks[HandLandmarkIndex::INDEX_TIP].y = 0.5f;
    ASSERT_TRUE(hand.isPinching(), "T6: isPinching when tips overlap");

    // isPinching: tips far apart
    hand.landmarks[HandLandmarkIndex::INDEX_TIP].x = 0.9f;
    ASSERT_TRUE(!hand.isPinching(), "T7: not pinching when tips far apart");

    // isVictoryGesture: index+middle extended (tip.y << mcp.y), ring+pinky bent
    hand.landmarks[HandLandmarkIndex::INDEX_MCP].y  = 0.6f;
    hand.landmarks[HandLandmarkIndex::INDEX_TIP].y  = 0.3f;   // extended
    hand.landmarks[HandLandmarkIndex::MIDDLE_MCP].y = 0.6f;
    hand.landmarks[HandLandmarkIndex::MIDDLE_TIP].y = 0.3f;   // extended
    hand.landmarks[HandLandmarkIndex::RING_MCP].y   = 0.6f;
    hand.landmarks[HandLandmarkIndex::RING_TIP].y   = 0.6f;   // bent (same level)
    hand.landmarks[HandLandmarkIndex::PINKY_MCP].y  = 0.6f;
    hand.landmarks[HandLandmarkIndex::PINKY_TIP].y  = 0.6f;   // bent
    ASSERT_TRUE(hand.isVictoryGesture(), "T8: isVictoryGesture correct");
}

// ---------------------------------------------------------------------------
// Test 8-10: Clip::MediaType TEXT / STICKER
// ---------------------------------------------------------------------------
static void testMediaTypes() {
    std::cout << "\n=== Clip::MediaType TEXT/STICKER ===\n";

    // SubtitleClip must use TEXT
    auto sub = std::make_shared<SubtitleClip>("s0", "Hello");
    ASSERT_EQ(sub->getType(), Clip::MediaType::TEXT,
              "T9: SubtitleClip type == TEXT");

    // StickerClip must use STICKER
    auto stk = std::make_shared<StickerClip>("k0", "sticker.png");
    ASSERT_EQ(stk->getType(), Clip::MediaType::STICKER,
              "T10: StickerClip type == STICKER");

    // Basic Clip with new types
    Clip textClip("t1", "", Clip::MediaType::TEXT);
    ASSERT_EQ(textClip.getType(), Clip::MediaType::TEXT,
              "T11: Clip(TEXT) round-trip");

    Clip stickerClip("t2", "", Clip::MediaType::STICKER);
    ASSERT_EQ(stickerClip.getType(), Clip::MediaType::STICKER,
              "T12: Clip(STICKER) round-trip");
}

// ---------------------------------------------------------------------------
// Test 11: SegmentationFilter::Mode enum accessible
// ---------------------------------------------------------------------------
static void testSegmentationModes() {
    std::cout << "\n=== SegmentationFilter::Mode ===\n";

    ASSERT_EQ(static_cast<int>(SegmentationFilter::Mode::BLUR),       0, "T13: BLUR==0");
    ASSERT_EQ(static_cast<int>(SegmentationFilter::Mode::BG_COLOR),    1, "T14: BG_COLOR==1");
    ASSERT_EQ(static_cast<int>(SegmentationFilter::Mode::TRANSPARENT), 2, "T15: TRANSPARENT==2");
    ASSERT_EQ(static_cast<int>(SegmentationFilter::Mode::BG_IMAGE),    3, "T16: BG_IMAGE==3");
    ASSERT_EQ(static_cast<int>(SegmentationFilter::Mode::ORIGINAL),    4, "T17: ORIGINAL==4");
}

// ---------------------------------------------------------------------------
// Test 12-14: AutoSubtitleEngine
// ---------------------------------------------------------------------------
static void testAutoSubtitleEngine() {
    std::cout << "\n=== AutoSubtitleEngine ===\n";

    // T17: createDefault returns stub (no factory registered)
    auto engine = AutoSubtitleEngine::createDefault();
    ASSERT_TRUE(engine != nullptr, "T17: createDefault returns non-null");

    // T18: stub recognize returns empty
    auto segs = engine->recognize("/tmp/audio.pcm", 10'000'000'000LL);
    ASSERT_EQ((int)segs.size(), 0, "T18: stub recognize returns empty");

    // T19: language/model path setter round-trip
    engine->setLanguage("en");
    ASSERT_EQ(engine->getLanguage(), std::string("en"), "T19: language setter");
    engine->setModelPath("/data/models/vosk-zh");
    ASSERT_EQ(engine->getModelPath(), std::string("/data/models/vosk-zh"),
              "T20: modelPath setter");

    // T21: register custom factory
    AutoSubtitleEngine::registerFactory([]() {
        auto e = std::make_shared<StubAutoSubtitleEngine>();
        return e;
    });
    auto engine2 = AutoSubtitleEngine::createDefault();
    ASSERT_TRUE(engine2 != nullptr, "T21: custom factory creates engine");

    // Reset factory (register null-returning factory → falls back to stub)
    AutoSubtitleEngine::registerFactory([]() -> std::shared_ptr<AutoSubtitleEngine> {
        return nullptr;  // will fall back to StubAutoSubtitleEngine
    });
}

// ---------------------------------------------------------------------------
// Test 15-19: TemplateEngine
// ---------------------------------------------------------------------------
static const char* kSampleJson = R"({
  "id": "test_tmpl",
  "name": "测试模板",
  "version": 1,
  "duration_ms": 10000,
  "fps": 30,
  "width": 1080,
  "height": 1920,
  "slots": [
    {"id": "v0", "type": "video", "duration_ms": 5000,
     "transition": "crossfade", "trans_dur_ms": 500, "fit": "crop_center"},
    {"id": "v1", "type": "video", "duration_ms": 5000,
     "transition": "wipe_left",  "trans_dur_ms": 400, "fit": "crop_center"}
  ],
  "subtitles": [
    {"id": "sub_0", "text": "你好世界", "start_ms": 0, "end_ms": 2000,
     "font_size": 56, "color": 4294967295, "y": 0.9, "align": 1}
  ],
  "audio": {"type": "music", "path": "assets/music/test.mp3",
            "loop": true, "volume": 0.8},
  "lut": {"path": "assets/luts/warm.png", "intensity": 0.6}
})";

static void testTemplateEngine() {
    std::cout << "\n=== TemplateEngine ===\n";

    // T22: parse JSON
    VideoTemplate tmpl;
    bool ok = TemplateEngine::loadFromString(kSampleJson, tmpl);
    ASSERT_TRUE(ok, "T22: loadFromString succeeds");
    ASSERT_EQ(tmpl.id, std::string("test_tmpl"), "T23: id parsed");
    ASSERT_EQ(tmpl.fps, 30, "T24: fps parsed");
    ASSERT_EQ((int)tmpl.slots.size(), 2, "T25: 2 slots parsed");
    ASSERT_EQ((int)tmpl.subtitles.size(), 1, "T26: 1 subtitle parsed");
    ASSERT_EQ(tmpl.audio.type, std::string("music"), "T27: audio.type");
    ASSERT_EQ(tmpl.slots[0].id, std::string("v0"), "T28: slot[0].id");
    ASSERT_EQ(tmpl.slots[0].durationMs, (int64_t)5000, "T29: slot[0].durationMs");
    ASSERT_EQ(tmpl.slots[1].transition, std::string("wipe_left"), "T30: slot[1].transition");

    // T31: allSlotsFilled false before filling
    ASSERT_TRUE(!tmpl.allSlotsFilled(), "T31: allSlotsFilled false initially");

    // T32: fill slots
    tmpl.slots[0].filledPath = "/sdcard/clip1.mp4";
    tmpl.slots[1].filledPath = "/sdcard/clip2.mp4";
    ASSERT_TRUE(tmpl.allSlotsFilled(), "T32: allSlotsFilled true after fill");

    // T33: apply() returns non-null Timeline
    TemplateEngine engine;
    auto timeline = engine.apply(tmpl);
    ASSERT_TRUE(timeline != nullptr, "T33: apply returns non-null timeline");

    // T34: resolution matches template
    ASSERT_EQ(timeline->getOutputWidth(),  1080, "T34: width==1080");
    ASSERT_EQ(timeline->getOutputHeight(), 1920, "T35: height==1920");

    // T36: main video track has 2 clips
    auto videoTrack = timeline->getTrack(0);
    ASSERT_TRUE(videoTrack != nullptr, "T36: video track exists at z=0");
    ASSERT_EQ((int)videoTrack->getAllClips().size(), 2,
              "T37: video track has 2 clips");

    // T38: subtitle track exists at z=10
    auto subTrack = timeline->getTrack(10);
    ASSERT_TRUE(subTrack != nullptr, "T38: subtitle track at z=10");
    ASSERT_EQ((int)subTrack->getAllClips().size(), 1, "T39: 1 subtitle clip");

    // T40: subtitle clip has correct MediaType
    auto subClip = subTrack->getAllClips()[0];
    ASSERT_EQ(subClip->getType(), Clip::MediaType::TEXT, "T40: subtitle is TEXT");

    // T41: apply with unfilled paths doesn't crash (placeholder paths)
    VideoTemplate tmpl2;
    TemplateEngine::loadFromString(kSampleJson, tmpl2);
    // do NOT fill paths
    auto tl2 = engine.apply(tmpl2);
    ASSERT_TRUE(tl2 != nullptr, "T41: apply with empty filledPath doesn't crash");

    // T42: audio track present
    auto audioTrack = timeline->getTrack(1);
    ASSERT_TRUE(audioTrack != nullptr, "T42: audio track at z=1");
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== test_phase1_gaps ===\n";

    testHandLandmarkDetector();
    testHandResultGestures();
    testMediaTypes();
    testSegmentationModes();
    testAutoSubtitleEngine();
    testTemplateEngine();

    std::cout << "\n\033[32mAll Phase 1 gap tests PASSED.\033[0m\n";
    return 0;
}
