#include <iostream>
#include <cassert>
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include "../core/include/timeline/DecoderPool.h"
#include "../core/include/timeline/VideoDecoder.h"

using namespace sdk::video;
using namespace sdk::video::timeline;

// Global flags to control mock behavior
static bool g_mockShouldFailSeek = false;
static bool g_mockShouldFailGetFrame = false;
static int g_mockFailErrorCode = ErrorCode::ERR_DECODER_HW_FAILURE;

// Mock Platform Decoder for testing
class MockVideoDecoder : public VideoDecoder {
public:
    Result open(const std::string& filePath) override {
        m_filePath = filePath;
        m_isOpen = true;
        m_openCount++;
        return Result::ok();
    }
    ResultPayload<Texture> getFrameAt(int64_t timeNs) override {
        if (!m_isOpen) return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Mock decoder not open");
        if (g_mockShouldFailGetFrame) {
            return ResultPayload<Texture>::error(g_mockFailErrorCode, "Mock failure");
        }
        return ResultPayload<Texture>::ok({m_id, 1920, 1080});
    }

    Result seekExact(int64_t timeNs) override {
        if (g_mockShouldFailSeek) {
            return Result::error(ErrorCode::ERR_DECODER_SEEK_FAILED, "Mock seek failure");
        }
        return Result::ok();
    }

    void close() override {
        m_isOpen = false;
        m_closeCount++;
    }

    static int m_openCount;
    static int m_closeCount;
    bool m_isOpen = false;
    std::string m_filePath;
    uint32_t m_id = 1;
};

int MockVideoDecoder::m_openCount = 0;
int MockVideoDecoder::m_closeCount = 0;

// Override createPlatformDecoder for testing
namespace sdk {
namespace video {
namespace timeline {
std::shared_ptr<VideoDecoder> createPlatformDecoder() {
    return std::make_shared<MockVideoDecoder>();
}
}
}
}

void test_decoder_pool_lru_eviction() {
    std::cout << "Running test_decoder_pool_lru_eviction..." << std::endl;
    MockVideoDecoder::m_openCount = 0;
    MockVideoDecoder::m_closeCount = 0;
    g_mockShouldFailSeek = false;
    g_mockShouldFailGetFrame = false;

    DecoderPool pool;
    // Register 5 media (limit is 4)
    pool.registerMedia("clip1", "v1.mp4");
    pool.registerMedia("clip2", "v2.mp4");
    pool.registerMedia("clip3", "v3.mp4");
    pool.registerMedia("clip4", "v4.mp4");
    pool.registerMedia("clip5", "v5.mp4");

    // Access 1-4
    assert(pool.getFrame("clip1", 0, false).isOk());
    assert(pool.getFrame("clip2", 0, false).isOk());
    assert(pool.getFrame("clip3", 0, false).isOk());
    assert(pool.getFrame("clip4", 0, false).isOk());

    assert(MockVideoDecoder::m_openCount == 4);
    assert(MockVideoDecoder::m_closeCount == 0);

    // Access 5, should evict clip1
    assert(pool.getFrame("clip5", 0, false).isOk());
    assert(MockVideoDecoder::m_openCount == 5);
    assert(MockVideoDecoder::m_closeCount == 1);

    // Access 1 again, should evict clip2
    assert(pool.getFrame("clip1", 0, false).isOk());
    assert(MockVideoDecoder::m_openCount == 6);
    assert(MockVideoDecoder::m_closeCount == 2);

    std::cout << "test_decoder_pool_lru_eviction passed" << std::endl;
}

void test_decoder_pool_release() {
    std::cout << "Running test_decoder_pool_release..." << std::endl;
    MockVideoDecoder::m_openCount = 0;
    MockVideoDecoder::m_closeCount = 0;
    g_mockShouldFailSeek = false;
    g_mockShouldFailGetFrame = false;

    {
        DecoderPool pool;
        pool.registerMedia("clip1", "v1.mp4");
        assert(pool.getFrame("clip1", 0, false).isOk());
        assert(MockVideoDecoder::m_openCount == 1);

        pool.releaseMedia("clip1");
        assert(MockVideoDecoder::m_closeCount == 1);
    }
    assert(MockVideoDecoder::m_closeCount == 1);

    std::cout << "test_decoder_pool_release passed" << std::endl;
}

void test_soft_decoder_lifecycle() {
    std::cout << "Running test_soft_decoder_lifecycle..." << std::endl;
    DecoderPool pool;
    pool.registerMedia("clip1", "v1.mp4");

    // Request frame with exact seek to trigger soft decoder
    auto res = pool.getFrame("clip1", 0, true);
    assert(res.isOk());
    assert(res.getValue().id == 0); // SoftwareVideoDecoder returns id 0 by default

    pool.releaseMedia("clip1");
    std::cout << "test_soft_decoder_lifecycle passed" << std::endl;
}

void test_hw_to_sw_fallback_on_seek_failure() {
    std::cout << "Running test_hw_to_sw_fallback_on_seek_failure..." << std::endl;
    MockVideoDecoder::m_openCount = 0;
    MockVideoDecoder::m_closeCount = 0;

    g_mockShouldFailSeek = true;
    g_mockShouldFailGetFrame = false;

    DecoderPool pool;
    pool.registerMedia("clip1", "v1.mp4");

    // This should attempt HW, fail seek, then fall back to SW
    auto res = pool.getFrame("clip1", 0, false);
    assert(res.isOk());
    assert(res.getValue().id == 0); // Fell back to SW which returns 0
    assert(MockVideoDecoder::m_openCount == 1);
    assert(MockVideoDecoder::m_closeCount == 1);

    // Subsequent calls should directly use SW
    auto res2 = pool.getFrame("clip1", 1000, false);
    assert(res2.isOk());
    assert(res2.getValue().id == 0);
    assert(MockVideoDecoder::m_openCount == 1); // No new HW decoder opened

    std::cout << "test_hw_to_sw_fallback_on_seek_failure passed" << std::endl;
}

void test_hw_to_sw_fallback_on_fatal_get_frame_failure() {
    std::cout << "Running test_hw_to_sw_fallback_on_fatal_get_frame_failure..." << std::endl;
    MockVideoDecoder::m_openCount = 0;
    MockVideoDecoder::m_closeCount = 0;

    g_mockShouldFailSeek = false;
    g_mockShouldFailGetFrame = true;
    g_mockFailErrorCode = ErrorCode::ERR_DECODER_HW_FAILURE; // Fatal

    DecoderPool pool;
    pool.registerMedia("clip1", "v1.mp4");

    // This should attempt HW, fail getFrameAt with fatal error, then fall back to SW
    auto res = pool.getFrame("clip1", 0, false);
    assert(res.isOk());
    assert(res.getValue().id == 0); // Fell back to SW
    assert(MockVideoDecoder::m_openCount == 1);
    assert(MockVideoDecoder::m_closeCount == 1);

    std::cout << "test_hw_to_sw_fallback_on_fatal_get_frame_failure passed" << std::endl;
}

void test_no_fallback_on_non_fatal_failure() {
    std::cout << "Running test_no_fallback_on_non_fatal_failure..." << std::endl;
    MockVideoDecoder::m_openCount = 0;
    MockVideoDecoder::m_closeCount = 0;

    g_mockShouldFailSeek = false;
    g_mockShouldFailGetFrame = true;
    g_mockFailErrorCode = ErrorCode::ERR_DECODER_FRAME_DROP; // Non-fatal

    DecoderPool pool;
    pool.registerMedia("clip1", "v1.mp4");

    auto res = pool.getFrame("clip1", 0, false);
    assert(!res.isOk());
    assert(res.getErrorCode() == ErrorCode::ERR_DECODER_FRAME_DROP);
    assert(MockVideoDecoder::m_openCount == 1);
    assert(MockVideoDecoder::m_closeCount == 0); // HW decoder NOT closed

    std::cout << "test_no_fallback_on_non_fatal_failure passed" << std::endl;
}

int main() {
    test_decoder_pool_lru_eviction();
    test_decoder_pool_release();
    test_soft_decoder_lifecycle();
    test_hw_to_sw_fallback_on_seek_failure();
    test_hw_to_sw_fallback_on_fatal_get_frame_failure();
    test_no_fallback_on_non_fatal_failure();
    return 0;
}
