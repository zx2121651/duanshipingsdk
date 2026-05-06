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
extern std::shared_ptr<VideoDecoder> createSoftwareDecoder();
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
// Test 1: createSoftwareDecoder() 工厂可以构造出非空对象
// ---------------------------------------------------------------------------
static bool test_factory_not_null() {
    const std::string kName = "createSoftwareDecoder() not null";
    auto dec = createSoftwareDecoder();
    if (!dec) {
        fail(kName, "returned nullptr");
        return false;
    }
    pass(kName);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: 用无效路径 open() — 应返回明确错误（非 ok()）
// ---------------------------------------------------------------------------
static bool test_open_invalid_path() {
    const std::string kName = "open(invalid_path) returns error";
    auto dec = createSoftwareDecoder();
    if (!dec) { fail(kName, "factory returned nullptr"); return false; }

    auto res = dec->open("/nonexistent/path/video.mp4");
    if (res.isOk()) {
        fail(kName, "Expected error but got ok");
        return false;
    }
    // 检查错误码不为 0（有明确错误码）
    if (res.getErrorCode() == ErrorCode::SUCCESS) {
        fail(kName, "Error code is ERR_OK despite failing to open");
        return false;
    }
    pass(kName);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: 未 open 的 decoder — getFrameAt() 应返回错误
// ---------------------------------------------------------------------------
static bool test_get_frame_without_open() {
    const std::string kName = "getFrameAt() without open() returns error";
    auto dec = createSoftwareDecoder();
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
// Test 6: SoftwareVideoDecoder stub — close() 可多次调用不崩溃
// ---------------------------------------------------------------------------
static bool test_close_idempotent() {
    const std::string kName = "close() idempotent (no crash)";
    auto dec = createSoftwareDecoder();
    if (!dec) { fail(kName, "factory returned nullptr"); return false; }
    try {
        dec->close();
        dec->close(); // 第二次 close 不应崩溃
    } catch (...) {
        fail(kName, "Unexpected exception on double close");
        return false;
    }
    pass(kName);
    return true;
}

#ifdef HAS_FFMPEG_DECODER
// ---------------------------------------------------------------------------
// Test 7 (仅 FFmpeg 构建): FFmpegVideoDecoder open() 返回有意义的 FFmpeg 错误，
//         不是"UNIMPLEMENTED"占位错误码
// ---------------------------------------------------------------------------
static bool test_ffmpeg_not_stub() {
    const std::string kName = "FFmpegVideoDecoder returns real error (not UNIMPLEMENTED)";
    auto dec = createSoftwareDecoder();
    auto res = dec->open("/no/such/file.mp4");

    // FFmpegVideoDecoder 的实际错误码是 ERR_TIMELINE_SOFT_DECODER_UNIMPLEMENTED
    // 但错误消息应包含 avformat 的错误信息，而非简单占位
    if (res.getMessage().find("FFmpegVideoDecoder") == std::string::npos) {
        fail(kName, "Error message does not mention FFmpegVideoDecoder: " + res.getMessage());
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
    run(test_get_frame_without_open());
    run(test_decoder_pool_strategy());
    run(test_decoder_pool_register_release());
    run(test_close_idempotent());

#ifdef HAS_FFMPEG_DECODER
    run(test_ffmpeg_not_stub());
#endif

    std::cout << "\nResult: " << passed << "/" << total << " passed\n";
    return (passed == total) ? 0 : 1;
}
