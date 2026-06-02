/**
 * test_ffmpeg_decoder.cpp
 *
 * FFmpegVideoDecoder 单元测试。
 *
 * 分两个场景：
 *   1. 无 FFmpeg 编译（stub 路径）：验证 SoftwareVideoDecoder 返回明确错误码
 *   2. 有 FFmpeg 编译（HAS_FFMPEG_DECODER）：验证 open/seekExact/getFrameAt 基本流程
 *
 * 本测试不需要真实 GPU（无 GL 调用，采用 mock），
 * 文件 I/O 使用虚拟路径（open() 预期失败并返回有意义的错误码）。
 */

#include <cassert>
#include <iostream>
#include <string>
#include <memory>

#include "../core/include/timeline/VideoDecoder.h"
#include "../core/include/timeline/DecoderPool.h"

using namespace sdk::video;
using namespace sdk::video::timeline;

// Factory defined in DecoderPool.cpp (must be declared inside its own namespace)
namespace sdk { namespace video { namespace timeline {
extern std::shared_ptr<VideoDecoder> createSoftwareDecoder_FFmpeg();
} } }

// Desktop test stub — mirrors pattern in test_decoder_pool.cpp
namespace sdk { namespace video { namespace timeline {
struct MockHwDecoder : VideoDecoder {
    Result open(const std::string&) override {
        return Result::error(ErrorCode::ERR_DECODER_HW_FAILURE, "mock hw decoder");
    }
    ResultPayload<Texture> getFrameAt(int64_t) override {
        return ResultPayload<Texture>::error(ErrorCode::ERR_DECODER_HW_FAILURE, "mock hw");
    }
    Result seekExact(int64_t) override {
        return Result::error(ErrorCode::ERR_DECODER_HW_FAILURE, "mock hw");
    }
    void close() override {}
};
std::shared_ptr<VideoDecoder> createPlatformDecoder() {
    return std::make_shared<MockHwDecoder>();
}
#ifndef HAS_FFMPEG_DECODER
std::shared_ptr<VideoDecoder> createSoftwareDecoder_FFmpeg() {
    return std::make_shared<SoftwareVideoDecoder>();
}
#endif
} } }

// ---------------------------------------------------------------------------
// Helper: 颜色化打印
// ---------------------------------------------------------------------------
static void pass(const std::string& name) {
    std::cout << "\033[32m  [PASS]\033[0m " << name << "\n";
}
static void fail(const std::string& name, const std::string& msg) {
    std::cerr << "\033[31m  [FAIL]\033[0m " << name << " — " << msg << "\n";
}

// ---------------------------------------------------------------------------
// Test 1: 工厂返回检测 — createSoftwareDecoder() 必须返回非空对象
// ---------------------------------------------------------------------------
static bool test_factory_not_null() {
    const std::string kName = "Test 1: factory return check";
    auto dec = createSoftwareDecoder_FFmpeg();
    if (!dec) {
        fail(kName, "createSoftwareDecoder_FFmpeg() returned nullptr");
        return false;
    }
    pass(kName);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: 无效路径检测 — open() 预期返回 ERR_DECODER_OPEN_FAILED
// ---------------------------------------------------------------------------
static bool test_open_invalid_path() {
    const std::string kName = "Test 2: invalid path check";
    auto dec = createSoftwareDecoder_FFmpeg();
    if (!dec) { fail(kName, "factory returned nullptr"); return false; }

    auto res = dec->open("/nonexistent/path/video.mp4");
    if (res.isOk()) {
        fail(kName, "Expected error for invalid path, but got OK");
        return false;
    }

    // 无论是 FFmpeg 实现还是 Stub，失败时都不应返回 OK
    // 如果是 FFmpeg 实现，应返回具体的 OPEN_FAILED
#ifdef HAS_FFMPEG_DECODER
    if (res.getErrorCode() != ErrorCode::ERR_DECODER_OPEN_FAILED) {
        fail(kName, "Expected ERR_DECODER_OPEN_FAILED but got " + std::to_string(res.getErrorCode()));
        return false;
    }
#endif
    pass(kName);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: 未 open 的 decoder — getFrameAt() / seekExact() 应返回错误
// ---------------------------------------------------------------------------
static bool test_seek_without_open() {
    const std::string kName = "seekExact() without open() returns error";
    auto dec = createSoftwareDecoder_FFmpeg();
    if (!dec) { fail(kName, "factory returned nullptr"); return false; }

    auto res = dec->seekExact(1000);
    if (res.isOk()) {
        fail(kName, "Expected error but got ok");
        return false;
    }
    if (res.getMessage().find("not open") == std::string::npos) {
        fail(kName, "Expected error message to mention 'not open', but got: " + res.getMessage());
        return false;
    }
    pass(kName);
    return true;
}

static bool test_get_frame_without_open() {
    const std::string kName = "getFrameAt() without open() returns error";
    auto dec = createSoftwareDecoder_FFmpeg();
    if (!dec) { fail(kName, "factory returned nullptr"); return false; }

    auto res = dec->getFrameAt(0);
    if (res.isOk()) {
        fail(kName, "Expected error but got ok");
        return false;
    }
    pass(kName);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4: DecoderPool — 策略设置读写一致性
// ---------------------------------------------------------------------------
static bool test_decoder_pool_strategy() {
    const std::string kName = "DecoderPool strategy get/set roundtrip";
    DecoderPool pool;

    pool.setFallbackStrategy(DecoderFallbackStrategy::SW_FIRST);
    if (pool.getFallbackStrategy() != DecoderFallbackStrategy::SW_FIRST) {
        fail(kName, "Strategy not applied (SW_FIRST)");
        return false;
    }

    pool.setFallbackStrategy(DecoderFallbackStrategy::HW_ONLY);
    if (pool.getFallbackStrategy() != DecoderFallbackStrategy::HW_ONLY) {
        fail(kName, "Strategy not applied (HW_ONLY)");
        return false;
    }

    pool.setFallbackStrategy(DecoderFallbackStrategy::AUTO);
    if (pool.getFallbackStrategy() != DecoderFallbackStrategy::AUTO) {
        fail(kName, "Strategy not applied (AUTO)");
        return false;
    }

    pass(kName);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5: DecoderPool — registerMedia + releaseMedia 不崩溃
// ---------------------------------------------------------------------------
static bool test_decoder_pool_register_release() {
    const std::string kName = "DecoderPool registerMedia/releaseMedia no crash";
    try {
        DecoderPool pool;
        pool.registerMedia("clip_001", "/fake/path/video.mp4");
        pool.registerMedia("clip_002", "/fake/path/audio.aac");
        pool.releaseMedia("clip_001");
        pool.releaseMedia("clip_999"); // 不存在的 id，应安全忽略
    } catch (...) {
        fail(kName, "Unexpected exception");
        return false;
    }
    pass(kName);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6: Double Close 检测 — close() 可多次调用不崩溃且状态安全
// ---------------------------------------------------------------------------
static bool test_double_close() {
    const std::string kName = "Test 6: double close stability";
    auto dec = createSoftwareDecoder_FFmpeg();
    if (!dec) { fail(kName, "factory returned nullptr"); return false; }
    try {
        dec->close();
        dec->close(); // 连续调用 close() 不应产生崩溃或非法访问
    } catch (...) {
        fail(kName, "Crash or exception detected during double close");
        return false;
    }
    pass(kName);
    return true;
}

#ifdef HAS_FFMPEG_DECODER
// ---------------------------------------------------------------------------
// Test 7 (仅 FFmpeg 构建): 验证工厂返回的是 FFmpeg 实现而非 stub
// ---------------------------------------------------------------------------
static bool test_factory_returns_ffmpeg_decoder() {
    const std::string kName = "test_factory_returns_ffmpeg_decoder";
    auto dec = createSoftwareDecoder_FFmpeg();
    auto res = dec->open("/no/such/file.mp4");

    // FFmpegVideoDecoder 的错误消息应包含 "FFmpegVideoDecoder"
    if (res.getMessage().find("FFmpegVideoDecoder") == std::string::npos) {
        fail(kName, "Error message does not mention FFmpegVideoDecoder: " + res.getMessage());
        return false;
    }
    // 且错误码不应是 ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED
    if (res.getErrorCode() == ErrorCode::ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED) {
        fail(kName, "Factory returned SoftwareVideoDecoder stub despite HAS_FFMPEG_DECODER");
        return false;
    }
    pass(kName);
    return true;
}
#endif

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "\n=== test_ffmpeg_decoder ===\n";

#ifdef HAS_FFMPEG_DECODER
    std::cout << "  [Mode] FFmpeg integrated (HAS_FFMPEG_DECODER=1)\n\n";
#else
    std::cout << "  [Mode] FFmpeg not integrated — testing stub behavior\n\n";
#endif

    int passed = 0, total = 0;

    auto run = [&](bool ok) { ++total; if (ok) ++passed; };

    run(test_factory_not_null());
    run(test_open_invalid_path());
    run(test_seek_without_open());
    run(test_get_frame_without_open());
    run(test_decoder_pool_strategy());
    run(test_decoder_pool_register_release());
    run(test_double_close());

#ifdef HAS_FFMPEG_DECODER
    run(test_factory_returns_ffmpeg_decoder());
#endif

    std::cout << "\nResult: " << passed << "/" << total << " passed\n";
    return (passed == total) ? 0 : 1;
}
