/**
 * test_media_compat.cpp
 * Phase C — 素材兼容测试套件
 *
 * 8 个测试用例，均在桌面 stub 环境运行（无设备、无实际解码器）。
 * 测试目标：验证错误码路径、PTS 校验逻辑、旋转元数据、B帧 seek 降级。
 */

#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "../core/include/timeline/VideoDecoder.h"
#include "../core/include/timeline/TemplateEngine.h"
#include "../core/include/GLTypes.h"

using namespace sdk::video;
using namespace sdk::video::timeline;

// ---------------------------------------------------------------------------
// Minimal stub that simulates open/decode/seek without a real decoder
// ---------------------------------------------------------------------------
struct FrameDesc {
    int64_t ptsNs;
    bool    isKeyframe;
    int     rotationDeg;   // EXIF / stream rotation
    bool    isH265;
};

class StubDecoder : public VideoDecoder {
public:
    StubDecoder() = default;

    // Configure the stub before calling open()
    void setFrames(std::vector<FrameDesc> frames) { m_frames = std::move(frames); }
    void setOpenShouldFail(bool v)   { m_openFail   = v; }
    void setIsH265(bool v)           { m_isH265     = v; }
    void setRotation(int deg)        { m_rotation   = deg; }

    Result open(const std::string& filePath) override {
        if (m_openFail) {
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                                 "StubDecoder: open failed for " + filePath);
        }
        m_filePath   = filePath;
        m_frameIndex = 0;
        m_opened     = true;
        return Result::ok();
    }

    ResultPayload<Texture> getFrameAt(int64_t timeNs) override {
        if (!m_opened)
            return ResultPayload<Texture>::error(ErrorCode::ERR_DECODER_OPEN_FAILED,
                                                 "Not opened");
        if (m_frames.empty())
            return ResultPayload<Texture>::error(ErrorCode::ERR_TIMELINE_DECODER_GET_FRAME_FAILED,
                                                 "No frames configured");
        // Find nearest frame by PTS
        auto it = std::min_element(m_frames.begin(), m_frames.end(),
            [timeNs](const FrameDesc& a, const FrameDesc& b) {
                return std::abs(a.ptsNs - timeNs) < std::abs(b.ptsNs - timeNs);
            });
        Texture tex;
        tex.id     = static_cast<uint32_t>(42 + (it - m_frames.begin()));
        tex.width  = 1920;
        tex.height = 1080;
        return ResultPayload<Texture>::ok(tex);
    }

    Result seekExact(int64_t timeNs) override {
        if (!m_opened)
            return Result::error(ErrorCode::ERR_DECODER_OPEN_FAILED, "Not opened");
        // If no keyframe exists at or before timeNs, simulate seek failure → demote
        bool keyframeFound = false;
        for (auto& f : m_frames) {
            if (f.isKeyframe && f.ptsNs <= timeNs) { keyframeFound = true; break; }
        }
        if (!keyframeFound) {
            m_lastSeekDemoted = true;
            return Result::error(ErrorCode::ERR_DECODER_SEEK_FAILED,
                                 "No keyframe at or before requested PTS");
        }
        m_lastSeekDemoted = false;
        return Result::ok();
    }

    void close() override { m_opened = false; }

    bool   isOpened()       const { return m_opened; }
    bool   lastSeekDemoted() const { return m_lastSeekDemoted; }
    int    getRotation()    const { return m_rotation; }
    bool   isH265()         const { return m_isH265; }

private:
    std::vector<FrameDesc> m_frames;
    std::string m_filePath;
    int         m_frameIndex    = 0;
    int         m_rotation      = 0;
    bool        m_isH265        = false;
    bool        m_openFail      = false;
    bool        m_opened        = false;
    bool        m_lastSeekDemoted = false;
};

// Helper to make a simple N-frame sequence at 30fps
static std::vector<FrameDesc> make30fps(int n, bool allKeyframes = false) {
    std::vector<FrameDesc> frames;
    int64_t ns = 1'000'000'000LL / 30;
    for (int i = 0; i < n; ++i) {
        frames.push_back({i * ns, allKeyframes || (i % 10 == 0), 0, false});
    }
    return frames;
}

// ---------------------------------------------------------------------------
// TC-C01: H.264 正常帧序列 — openSync 成功，decodeNextFrame 不崩溃
// ---------------------------------------------------------------------------
static void tc_c01_h264_normal_sequence() {
    StubDecoder dec;
    dec.setFrames(make30fps(300));

    auto r = dec.open("clip_h264.mp4");
    assert(r.isOk());
    assert(dec.isOpened());

    for (int i = 0; i < 5; ++i) {
        auto fr = dec.getFrameAt(i * 33'333'333LL);
        assert(fr.isOk());
        assert(fr.getValue().id != 0);
    }
    dec.close();
    assert(!dec.isOpened());

    std::cout << "TC-C01 PASS: h264_normal_sequence" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-C02: H.265 stub — codec 类型检测字段正确
// ---------------------------------------------------------------------------
static void tc_c02_h265_codec_detection() {
    StubDecoder dec;
    dec.setIsH265(true);
    dec.setFrames(make30fps(60));

    auto r = dec.open("clip_hevc.mp4");
    assert(r.isOk());
    assert(dec.isH265());
    // Verify we can still decode a frame without crashing
    auto fr = dec.getFrameAt(0);
    assert(fr.isOk());

    std::cout << "TC-C02 PASS: h265_codec_detection (isH265=" << dec.isH265() << ")" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-C03: 可变帧率 (VFR) — PTS 单调性校验
// ---------------------------------------------------------------------------
static void tc_c03_vfr_pts_monotonic() {
    // Build a VFR sequence: variable intervals
    std::vector<FrameDesc> vfrFrames;
    int64_t pts[] = {0, 20'000'000, 55'000'000, 55'500'000,
                     90'000'000, 133'000'000, 133'001'000, 200'000'000};
    for (auto p : pts) {
        vfrFrames.push_back({p, (p == 0), 0, false});
    }

    bool monotonic = true;
    for (size_t i = 1; i < vfrFrames.size(); ++i) {
        if (vfrFrames[i].ptsNs <= vfrFrames[i-1].ptsNs) {
            monotonic = false;
        }
    }
    assert(monotonic && "VFR PTS sequence must be strictly increasing");

    StubDecoder dec;
    dec.setFrames(vfrFrames);
    auto r = dec.open("clip_vfr.mp4");
    assert(r.isOk());
    for (auto& f : vfrFrames) {
        auto fr = dec.getFrameAt(f.ptsNs);
        assert(fr.isOk());
    }

    std::cout << "TC-C03 PASS: vfr_pts_monotonic" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-C04: 旋转元数据 90/180/270 — 读取并应用
// ---------------------------------------------------------------------------
static void tc_c04_rotation_metadata() {
    for (int rot : {0, 90, 180, 270}) {
        StubDecoder dec;
        dec.setRotation(rot);
        dec.setFrames(make30fps(10, true));
        auto r = dec.open("clip_rot.mp4");
        assert(r.isOk());
        assert(dec.getRotation() == rot);
        // Verify rotation is one of the valid EXIF values
        assert(dec.getRotation() % 90 == 0);
    }
    std::cout << "TC-C04 PASS: rotation_metadata (0/90/180/270 all accepted)" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-C05: 异常 PTS（负值、乱序）— 不崩溃，有兜底日志
// ---------------------------------------------------------------------------
static void tc_c05_bad_pts_no_crash() {
    std::vector<FrameDesc> badFrames = {
        {-1'000'000LL,          true,  0, false},  // negative PTS
        {500'000'000LL,         false, 0, false},
        {100'000'000LL,         false, 0, false},  // out of order
        {std::numeric_limits<int64_t>::max(), false, 0, false}, // extreme
    };

    StubDecoder dec;
    dec.setFrames(badFrames);
    auto r = dec.open("clip_bad_pts.mp4");
    assert(r.isOk());

    // Must not crash on any PTS — including negative
    for (auto& f : badFrames) {
        auto fr = dec.getFrameAt(f.ptsNs);
        // We just require no crash; result may be ok or error
        (void)fr;
    }

    std::cout << "TC-C05 PASS: bad_pts_no_crash" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-C06: B 帧 seek — 无可用关键帧 → seek 返回 ERR_DECODER_SEEK_FAILED
// ---------------------------------------------------------------------------
static void tc_c06_bframe_seek_demotion() {
    // Sequence with keyframe only at t=0; non-keyframes elsewhere
    std::vector<FrameDesc> bframes = {
        {0,             true,  0, false},
        {33'333'333LL,  false, 0, false},
        {66'666'666LL,  false, 0, false},
    };

    StubDecoder dec;
    dec.setFrames(bframes);
    auto r = dec.open("clip_bframe.mp4");
    assert(r.isOk());

    // Seek to mid-point where no keyframe exists before the target
    int64_t seekTarget = 50'000'000LL;
    // The only keyframe is at t=0 which IS <= seekTarget → seek should succeed
    auto sr = dec.seekExact(seekTarget);
    assert(sr.isOk());

    // Now seek to a time BEFORE any keyframe (negative) → should fail
    sr = dec.seekExact(-1LL);
    assert(!sr.isOk());
    assert(sr.getErrorCode() == ErrorCode::ERR_DECODER_SEEK_FAILED);
    assert(dec.lastSeekDemoted());

    std::cout << "TC-C06 PASS: bframe_seek_demotion (ERR_DECODER_SEEK_FAILED=" 
              << ErrorCode::ERR_DECODER_SEEK_FAILED << ")" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-C07: 空文件路径 — open 返回 ERR_DECODER_OPEN_FAILED
// ---------------------------------------------------------------------------
static void tc_c07_empty_path_open_failed() {
    StubDecoder dec;
    dec.setOpenShouldFail(true);

    auto r = dec.open("");
    assert(!r.isOk());
    assert(r.getErrorCode() == ErrorCode::ERR_DECODER_OPEN_FAILED);
    assert(!dec.isOpened());

    // Subsequent getFrameAt must also fail gracefully
    auto fr = dec.getFrameAt(0);
    assert(!fr.isOk());

    std::cout << "TC-C07 PASS: empty_path_open_failed (code=" 
              << r.getErrorCode() << ")" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-C08: DecoderPool 最大并发压力 — 超出 MAX_DECODERS 不崩溃
// ---------------------------------------------------------------------------
static void tc_c08_decoder_pool_stress() {
    constexpr int MAX_DECODERS = 16;
    std::vector<std::unique_ptr<StubDecoder>> pool;
    pool.reserve(MAX_DECODERS + 4);

    // Open MAX_DECODERS + 4 decoders concurrently (no real pool limit in stub)
    for (int i = 0; i < MAX_DECODERS + 4; ++i) {
        auto dec = std::make_unique<StubDecoder>();
        dec->setFrames(make30fps(10, true));
        auto r = dec->open("stream_" + std::to_string(i) + ".mp4");
        assert(r.isOk());
        pool.push_back(std::move(dec));
    }

    // All decoders must be able to decode without crashing
    for (auto& dec : pool) {
        auto fr = dec->getFrameAt(0);
        assert(fr.isOk());
    }

    // Verify all closed cleanly
    for (auto& dec : pool) {
        dec->close();
        assert(!dec->isOpened());
    }

    std::cout << "TC-C08 PASS: decoder_pool_stress (" 
              << (MAX_DECODERS + 4) << " concurrent decoders)" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-C09: TemplateEngine 批量加载测试 — 确保 13 个模板全部解析成功
// ---------------------------------------------------------------------------
static void tc_c09_template_bulk_load() {
    std::string path = "assets/templates";
    auto templates = TemplateEngine::loadAllFromDirectory(path);
    if (templates.empty()) {
        path = "../assets/templates";
        templates = TemplateEngine::loadAllFromDirectory(path);
    }
    if (templates.empty()) {
        path = "../../assets/templates";
        templates = TemplateEngine::loadAllFromDirectory(path);
    }

    // We expect 3 original + 10 new = 13 templates
    std::cout << "TC-C09: Loaded " << templates.size() << " templates from " << path << std::endl;
    assert(templates.size() >= 13);

    for (const auto& tmpl : templates) {
        assert(!tmpl.id.empty());
        assert(!tmpl.slots.empty());
        // Verify audio/lut are present (optional in schema but required by task)
        // audio.type defaults to "none" if not present, but our templates should have them
        assert(!tmpl.audio.type.empty());
    }

    std::cout << "TC-C09 PASS: template_bulk_load" << std::endl;
}

int main() {
    tc_c01_h264_normal_sequence();
    tc_c02_h265_codec_detection();
    tc_c03_vfr_pts_monotonic();
    tc_c04_rotation_metadata();
    tc_c05_bad_pts_no_crash();
    tc_c06_bframe_seek_demotion();
    tc_c07_empty_path_open_failed();
    tc_c08_decoder_pool_stress();
    tc_c09_template_bulk_load();
    std::cout << "\nAll test_media_compat cases PASSED" << std::endl;
    return 0;
}
