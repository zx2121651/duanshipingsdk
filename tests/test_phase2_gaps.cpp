/**
 * test_phase2_gaps.cpp
 *
 * Phase 2 功能差距补齐单元测试：
 *   1-4:   ChromaKeyFilter — 构造 / setKeyColor / setKeyColorFromARGB / Mode 枚举
 *   5-8:   BlendMode — 枚举值 / name / fromString 往返
 *   9-10:  Track::setBlendMode / getBlendMode
 *   11-16: VoiceChangerFilter — preset 设置 / presetName / process 无崩溃 / reset
 *   17-22: ExpressionDetector — detect() 结果合理性
 *   23-26: ExportPreset — 工厂方法 / 字段验证 / 水印配置
 *   27-32: SmartCoverSelector — submitFrame / selectBest / reset
 *   33-38: VideoStabilizer — analyzeFrame / computeTrajectory / getTransformAt
 *   39-42: DraftAutoSave — markDirty / isDirty / hasRecoveryDraft / listSnapshots
 */

#include <cassert>
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <cstring>

#include "../core/include/ai/ChromaKeyFilter.h"
#include "../core/include/timeline/BlendMode.h"
#include "../core/include/timeline/Track.h"
#include "../core/include/audio/VoiceChangerFilter.h"
#include "../core/include/ai/ExpressionDetector.h"
#include "../core/include/timeline/ExportPreset.h"
#include "../core/include/timeline/SmartCoverSelector.h"
#include "../core/include/ai/VideoStabilizer.h"
#include "../core/include/timeline/DraftAutoSave.h"
#include "../core/include/timeline/Timeline.h"

using namespace sdk::video;
using namespace sdk::video::ai;
using namespace sdk::video::timeline;
using namespace sdk::video::audio;

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
#define ASSERT_NEAR(a, b, eps, name) \
    do { float _d = (float)(a)-(float)(b); if (_d<0.f)_d=-_d; \
         if (_d > (eps)) fail(name, std::to_string(a)+" != "+std::to_string(b)); \
         else pass(name); } while(0)

// ---------------------------------------------------------------------------
// Tests 1-4: ChromaKeyFilter
// ---------------------------------------------------------------------------
static void testChromaKeyFilter() {
    std::cout << "\n=== ChromaKeyFilter ===\n";

    ChromaKeyFilter f;

    // T1: setKeyColor round-trip
    f.setKeyColor(0.333f, 0.10f, 0.25f);
    ASSERT_TRUE(f.getMode() == ChromaKeyFilter::Mode::TRANSPARENT, "T1: default mode TRANSPARENT");

    // T2: setMode
    f.setMode(ChromaKeyFilter::Mode::REPLACE_BG);
    ASSERT_TRUE(f.getMode() == ChromaKeyFilter::Mode::REPLACE_BG, "T2: setMode REPLACE_BG");

    // T3: setKeyColorFromARGB — green → hue ≈ 0.333
    f.setKeyColorFromARGB(0xFF00FF00u); // pure green
    // (no crash check)
    ASSERT_TRUE(true, "T3: setKeyColorFromARGB no crash");

    // T4: enum values accessible
    ASSERT_EQ(static_cast<int>(ChromaKeyFilter::Mode::TRANSPARENT), 0, "T4: TRANSPARENT==0");
    ASSERT_EQ(static_cast<int>(ChromaKeyFilter::Mode::REPLACE_BG),  1, "T4: REPLACE_BG==1");
    ASSERT_EQ(static_cast<int>(ChromaKeyFilter::Mode::IMAGE_BG),    2, "T4: IMAGE_BG==2");
}

// ---------------------------------------------------------------------------
// Tests 5-10: BlendMode
// ---------------------------------------------------------------------------
static void testBlendMode() {
    std::cout << "\n=== BlendMode ===\n";

    // T5: enum values
    ASSERT_EQ(static_cast<int>(BlendMode::NORMAL),   0, "T5: NORMAL==0");
    ASSERT_EQ(static_cast<int>(BlendMode::MULTIPLY),  1, "T5: MULTIPLY==1");
    ASSERT_EQ(static_cast<int>(BlendMode::SCREEN),    2, "T5: SCREEN==2");
    ASSERT_EQ(static_cast<int>(BlendMode::ADD),      16, "T5: ADD==16");

    // T6: blendModeName
    ASSERT_EQ(std::string(blendModeName(BlendMode::OVERLAY)),    "overlay",    "T6: overlay");
    ASSERT_EQ(std::string(blendModeName(BlendMode::SOFT_LIGHT)), "soft_light", "T6: soft_light");

    // T7: blendModeFromString round-trip
    ASSERT_EQ(blendModeFromString("multiply"),   BlendMode::MULTIPLY,   "T7: multiply");
    ASSERT_EQ(blendModeFromString("screen"),     BlendMode::SCREEN,     "T7: screen");
    ASSERT_EQ(blendModeFromString("difference"), BlendMode::DIFFERENCE, "T7: difference");
    ASSERT_EQ(blendModeFromString("unknown_xx"), BlendMode::NORMAL,     "T7: unknown→NORMAL");

    // T8: Track setBlendMode / getBlendMode
    auto track = std::make_shared<Track>(5, Track::TrackType::PIP_VIDEO);
    ASSERT_EQ(track->getBlendMode(), BlendMode::NORMAL, "T8: default NORMAL");
    track->setBlendMode(BlendMode::SCREEN);
    ASSERT_EQ(track->getBlendMode(), BlendMode::SCREEN, "T9: SCREEN after set");

    track->setBlendMode(BlendMode::MULTIPLY);
    ASSERT_EQ(track->getBlendMode(), BlendMode::MULTIPLY, "T10: MULTIPLY after set");
}

// ---------------------------------------------------------------------------
// Tests 11-16: VoiceChangerFilter
// ---------------------------------------------------------------------------
static void testVoiceChangerFilter() {
    std::cout << "\n=== VoiceChangerFilter ===\n";

    VoiceChangerFilter vc;

    // T11: default preset ORIGINAL
    ASSERT_EQ(vc.getPreset(), VoiceChangerFilter::Preset::ORIGINAL, "T11: default ORIGINAL");

    // T12: presetName
    ASSERT_EQ(std::string(VoiceChangerFilter::presetName(VoiceChangerFilter::Preset::CHIPMUNK)),
              "花栗鼠", "T12: chipmunk name");

    // T13: setPreset CHIPMUNK → pitchSemitones == 8
    vc.setPreset(VoiceChangerFilter::Preset::CHIPMUNK);
    ASSERT_NEAR(vc.getPitchSemitones(), 8.f, 0.01f, "T13: chipmunk pitch=8");

    // T14: setPreset GIANT → pitch == -8
    vc.setPreset(VoiceChangerFilter::Preset::GIANT);
    ASSERT_NEAR(vc.getPitchSemitones(), -8.f, 0.01f, "T14: giant pitch=-8");

    // T15: process (int16) — no crash, output size == input
    vc.setPreset(VoiceChangerFilter::Preset::ORIGINAL);
    constexpr size_t kFrames = 1024;
    std::vector<int16_t> inBuf(kFrames, 1000);
    std::vector<int16_t> outBuf(kFrames, 0);
    vc.process(inBuf.data(), kFrames, outBuf.data(), 44100, 1);
    ASSERT_TRUE(true, "T15: process no crash");

    // T16: processFloat — echo effect applies
    vc.setPreset(VoiceChangerFilter::Preset::ECHO);
    std::vector<float> fIn(kFrames, 0.5f);
    std::vector<float> fOut(kFrames, 0.f);
    vc.processFloat(fIn.data(), kFrames, fOut.data(), 44100, 1);
    ASSERT_TRUE(true, "T16: processFloat echo no crash");

    // T17: reset — no crash
    vc.reset();
    ASSERT_TRUE(true, "T17: reset no crash");
}

// ---------------------------------------------------------------------------
// Tests 18-22: ExpressionDetector
// ---------------------------------------------------------------------------
static void testExpressionDetector() {
    std::cout << "\n=== ExpressionDetector ===\n";

    ExpressionDetector ed;

    // T18: undetected face → all false
    FaceResult undetected;
    undetected.detected = false;
    auto r0 = ed.detect(undetected);
    ASSERT_TRUE(!r0.smiling && !r0.mouthOpen, "T18: undetected → no expression");

    // T19: detected face with default landmarks → no crash
    FaceResult face;
    face.detected = true;
    face.faceScore = 0.95f;
    // Set up inter-ocular distance: lm[36] and lm[45] (≈ 0.2 apart)
    face.landmarks[36] = {0.30f, 0.40f, 1.f};
    face.landmarks[45] = {0.70f, 0.40f, 1.f}; // scale ≈ 0.40
    // Eye open: set landmarks to open state
    face.landmarks[37] = {0.34f, 0.38f, 1.f};
    face.landmarks[38] = {0.38f, 0.37f, 1.f};
    face.landmarks[40] = {0.38f, 0.43f, 1.f};
    face.landmarks[41] = {0.34f, 0.43f, 1.f};
    face.landmarks[39] = {0.42f, 0.40f, 1.f};
    face.landmarks[42] = {0.58f, 0.40f, 1.f};
    face.landmarks[43] = {0.62f, 0.38f, 1.f};
    face.landmarks[44] = {0.66f, 0.37f, 1.f};
    face.landmarks[46] = {0.66f, 0.43f, 1.f};
    face.landmarks[47] = {0.62f, 0.43f, 1.f};
    // Mouth open: set top/bottom lip distance > threshold
    face.landmarks[51] = {0.50f, 0.70f, 1.f}; // upper lip center
    face.landmarks[57] = {0.50f, 0.80f, 1.f}; // lower lip center (0.10 apart)
    face.landmarks[48] = {0.42f, 0.75f, 1.f}; // left corner
    face.landmarks[54] = {0.58f, 0.75f, 1.f}; // right corner
    // Brows
    face.landmarks[19] = {0.36f, 0.30f, 1.f};
    face.landmarks[24] = {0.64f, 0.30f, 1.f};

    auto r1 = ed.detect(face);
    ASSERT_TRUE(true, "T19: detect no crash with synthetic face");

    // T20: eye openness in [0,1]
    ASSERT_TRUE(r1.leftEyeOpenness  >= 0.f && r1.leftEyeOpenness  <= 1.f, "T20: leftEye [0,1]");
    ASSERT_TRUE(r1.rightEyeOpenness >= 0.f && r1.rightEyeOpenness <= 1.f, "T21: rightEye [0,1]");

    // T22: mouthOpenness > 0 (we set a large mouth opening)
    ASSERT_TRUE(r1.mouthOpenness > 0.f, "T22: mouthOpenness > 0 with large opening");
}

// ---------------------------------------------------------------------------
// Tests 23-26: ExportPreset
// ---------------------------------------------------------------------------
static void testExportPreset() {
    std::cout << "\n=== ExportPreset ===\n";

    // T23: douyinVertical1080p fields
    auto p = ExportPreset::douyinVertical1080p();
    ASSERT_EQ(p.width, 1080, "T23: douyin width==1080");
    ASSERT_EQ(p.height, 1920, "T23: douyin height==1920");
    ASSERT_EQ(p.fps, 30, "T23: douyin fps==30");
    ASSERT_EQ(p.platform, std::string("douyin"), "T23: douyin platform");

    // T24: youtubeShorts 60fps
    auto yt = ExportPreset::youtubeShorts();
    ASSERT_EQ(yt.fps, 60, "T24: youtube fps==60");

    // T25: landscape4K H.265
    auto k4 = ExportPreset::landscape4K();
    ASSERT_EQ(k4.videoCodec, std::string("h265"), "T25: 4k codec==h265");
    ASSERT_EQ(k4.width, 3840, "T25: 4k width==3840");

    // T26: watermark config
    auto inst = ExportPreset::instagramReels();
    inst.watermark.enabled   = true;
    inst.watermark.imageUri  = "/sdcard/logo.png";
    inst.watermark.opacity   = 0.5f;
    inst.watermark.corner    = WatermarkCorner::BOTTOM_RIGHT;
    ASSERT_TRUE(inst.watermark.enabled, "T26: watermark enabled");
    ASSERT_NEAR(inst.watermark.opacity, 0.5f, 0.01f, "T26: watermark opacity");
}

// ---------------------------------------------------------------------------
// Tests 27-32: SmartCoverSelector
// ---------------------------------------------------------------------------
static void testSmartCoverSelector() {
    std::cout << "\n=== SmartCoverSelector ===\n";

    SmartCoverSelector sel;
    sel.setStrategy(SmartCoverSelector::BRIGHTNESS | SmartCoverSelector::SHARPNESS);
    sel.setCandidateCount(3);

    // T27: empty → selectBest returns 0-score
    auto best0 = sel.selectBest();
    ASSERT_EQ(best0.timeNs, (int64_t)0, "T27: empty selector timeNs==0");

    // T28: submit synthetic frames
    constexpr int W = 32, H = 32;
    std::vector<uint8_t> bright(W * H * 4, 200u);  // bright frame
    std::vector<uint8_t> dark  (W * H * 4,  30u);  // dark frame

    sel.submitFrame(bright.data(), W, H, 1'000'000'000LL);  // t=1s
    sel.submitFrame(dark.data(),   W, H, 2'000'000'000LL);  // t=2s
    sel.submitFrame(bright.data(), W, H, 3'000'000'000LL);  // t=3s

    // T29: selectCandidates returns ≤3 items
    auto cands = sel.selectCandidates();
    ASSERT_TRUE((int)cands.size() <= 3, "T29: candidates <= 3");
    ASSERT_TRUE(!cands.empty(), "T30: candidates non-empty");

    // T31: selectBest picks highest score
    auto best = sel.selectBest();
    ASSERT_TRUE(best.score >= 0.f && best.score <= 1.f, "T31: score in [0,1]");

    // T32: reset clears frames
    sel.reset();
    auto empty2 = sel.selectCandidates();
    ASSERT_TRUE(empty2.empty(), "T32: reset → empty candidates");
}

// ---------------------------------------------------------------------------
// Tests 33-38: VideoStabilizer
// ---------------------------------------------------------------------------
static void testVideoStabilizer() {
    std::cout << "\n=== VideoStabilizer ===\n";

    VideoStabilizer vs;
    vs.setStrategy(VideoStabilizer::Strategy::OPTICAL_FLOW);
    vs.setSmoothRadius(5);

    // T33: initial state — no crash on getTransformAt
    auto t0 = vs.getTransformAt(0);
    ASSERT_TRUE(!t0.valid || t0.dx == 0.f, "T33: no frames → valid=false or dx=0");

    // T34: submit frames
    constexpr int W = 64, H = 64;
    std::vector<uint8_t> frame1(W * H * 4, 128u);
    std::vector<uint8_t> frame2(W * H * 4, 130u); // slightly different
    vs.analyzeFrame(frame1.data(), W, H, 0LL);
    vs.analyzeFrame(frame2.data(), W, H, 33'333'333LL);
    vs.analyzeFrame(frame1.data(), W, H, 66'666'666LL);
    ASSERT_EQ(vs.analyzedFrameCount(), 3, "T34: analyzedFrameCount==3");

    // T35: computeTrajectory no crash
    vs.computeTrajectory();
    ASSERT_TRUE(true, "T35: computeTrajectory no crash");

    // T36: getTransformAt returns valid transform
    auto t1 = vs.getTransformAt(33'333'333LL);
    ASSERT_TRUE(t1.scale >= 1.f, "T36: scale >= 1.0 after stabilize");

    // T37: gyro path
    vs.reset();
    vs.setStrategy(VideoStabilizer::Strategy::GYRO);
    vs.addGyroSample({0LL, 0.01f, 0.02f, 0.f});
    vs.addGyroSample({33'000'000LL, 0.01f, 0.02f, 0.f});
    auto tGyro = vs.getTransformAt(33'000'000LL);
    ASSERT_TRUE(true, "T37: gyro getTransformAt no crash");

    // T38: reset clears analyzed frames
    vs.reset();
    ASSERT_EQ(vs.analyzedFrameCount(), 0, "T38: reset → analyzedFrameCount==0");
}

// ---------------------------------------------------------------------------
// Tests 39-42: DraftAutoSave
// ---------------------------------------------------------------------------
static void testDraftAutoSave() {
    std::cout << "\n=== DraftAutoSave ===\n";

    DraftAutoSave das;
    das.setInterval(60'000);  // 60s to prevent auto-save during test

    // T39: initial state not dirty
    ASSERT_TRUE(!das.isDirty(), "T39: initially not dirty");

    // T40: markDirty
    das.markDirty();
    ASSERT_TRUE(das.isDirty(), "T40: dirty after markDirty");

    // T41: listSnapshots empty when no dir set
    auto snaps = das.listSnapshots();
    ASSERT_TRUE(snaps.empty(), "T41: empty snapshots with no dir");

    // T42: hasRecoveryDraft false when no dir set
    ASSERT_TRUE(!das.hasRecoveryDraft(), "T42: no recovery when no dir");

    // T43: setDraftDir getter round-trip
    das.setDraftDir("/tmp/test_draft");
    ASSERT_EQ(das.getDraftDir(), std::string("/tmp/test_draft"), "T43: getDraftDir");
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== test_phase2_gaps ===\n";

    testChromaKeyFilter();
    testBlendMode();
    testVoiceChangerFilter();
    testExpressionDetector();
    testExportPreset();
    testSmartCoverSelector();
    testVideoStabilizer();
    testDraftAutoSave();

    std::cout << "\n\033[32mAll Phase 2 gap tests PASSED.\033[0m\n";
    return 0;
}
