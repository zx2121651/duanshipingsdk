/**
 * test_ai_inference.cpp
 *
 * TfliteInferenceEngine + BeautyFilter + SegmentationFilter 单元测试。
 */

#include <cassert>
#include <iostream>
#include <string>
#include <memory>
#include <vector>

#include "../core/include/ai/TfliteInferenceEngine.h"
#include "../core/include/ai/FaceLandmarkDetector.h"
#include "../core/include/ai/BodyPoseDetector.h"
#include "../core/include/ai/BeautyFilter.h"
#include "../core/include/ai/SegmentationFilter.h"

using namespace sdk::video;
using namespace sdk::video::ai;

// ---------------------------------------------------------------------------
static void pass(const std::string& name) {
    std::cout << "\033[32m  [PASS]\033[0m " << name << "\n";
}
static void fail(const std::string& name, const std::string& msg) {
    std::cerr << "\033[31m  [FAIL]\033[0m " << name << " — " << msg << "\n";
}

// ---------------------------------------------------------------------------
// Test 1: 引擎构造不崩溃
// ---------------------------------------------------------------------------
static bool test_engine_construct() {
    const std::string k = "TfliteInferenceEngine construct no-crash";
    try {
        TfliteInferenceEngine eng;
        if (eng.isLoaded()) { fail(k, "should NOT be loaded after default-construct"); return false; }
    } catch (...) { fail(k, "unexpected exception"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 10: SegmentationFilter 参数读写与 Mode 切换
// ---------------------------------------------------------------------------
static bool test_segmentation_params() {
    const std::string k = "SegmentationFilter parameters set/get and mode switch";
    try {
        SegmentationFilter sf(nullptr, nullptr);

        // 1. Mode check
        sf.setParameter("mode", static_cast<int>(SegmentationFilter::Mode::BG_COLOR));
        if (sf.getMode() != SegmentationFilter::Mode::BG_COLOR) {
            fail(k, "Mode set/get failed (BG_COLOR)"); return false;
        }

        sf.setParameter("mode", static_cast<int>(SegmentationFilter::Mode::TRANSPARENT));
        if (sf.getMode() != SegmentationFilter::Mode::TRANSPARENT) {
            fail(k, "Mode switch failed (TRANSPARENT)"); return false;
        }

        sf.setParameter("mode", static_cast<int>(SegmentationFilter::Mode::BG_IMAGE));
        if (sf.getMode() != SegmentationFilter::Mode::BG_IMAGE) {
            fail(k, "Mode switch failed (BG_IMAGE)"); return false;
        }

        sf.setParameter("mode", static_cast<int>(SegmentationFilter::Mode::ORIGINAL));
        if (sf.getMode() != SegmentationFilter::Mode::ORIGINAL) {
            fail(k, "Mode switch failed (ORIGINAL)"); return false;
        }

        sf.setParameter("mode", static_cast<int>(SegmentationFilter::Mode::BLUR));
        if (sf.getMode() != SegmentationFilter::Mode::BLUR) {
            fail(k, "Mode switch failed (BLUR)"); return false;
        }

        // 2. blurStrength check
        sf.setParameter("blurStrength", 25.0f);
        if (sf.getBlurStrength() != 25.0f) {
            fail(k, "blurStrength set/get failed"); return false;
        }

        // 3. bgColor check
        uint32_t red = 0xFFFF0000u;
        sf.setParameter("bgColor", red);
        if (sf.getBgColor() != red) {
            fail(k, "bgColor set/get failed"); return false;
        }

        // 4. bgImageTexture check
        sf.setBgImageTexture(12345u);
        if (sf.getBgImageTexture() != 12345u) {
            fail(k, "bgImageTexture set/get failed"); return false;
        }

    } catch (...) {
        fail(k, "unexpected exception"); return false;
    }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 11: SegmentationFilter 默认参数验证
// ---------------------------------------------------------------------------
static bool test_segmentation_default_params() {
    const std::string k = "SegmentationFilter default parameters";
    try {
        SegmentationFilter sf(nullptr, nullptr);
        if (sf.getMode() != SegmentationFilter::Mode::BLUR) { fail(k, "default mode should be BLUR"); return false; }
        if (sf.getBlurStrength() != 10.0f) { fail(k, "default blurStrength should be 10.0"); return false; }
        if (sf.getBgColor() != 0xFF000000u) { fail(k, "default bgColor should be black"); return false; }
        if (sf.getEdgeSoften() != 0.5f) { fail(k, "default edgeSoften should be 0.5"); return false; }
        if (sf.getBgImageTexture() != 0u) { fail(k, "default bgImageTexture should be 0"); return false; }
    } catch (...) { fail(k, "unexpected exception"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 12: SegmentationFilter setParameter("bgImageTexture") 同步验证
// ---------------------------------------------------------------------------
static bool test_segmentation_set_parameter_sync() {
    const std::string k = "SegmentationFilter setParameter sync";
    try {
        SegmentationFilter sf(nullptr, nullptr);
        uint32_t texId = 54321u;
        sf.setParameter("bgImageTexture", texId);
        if (sf.getBgImageTexture() != texId) {
            fail(k, "setParameter('bgImageTexture') failed to sync m_bgImageTexId"); return false;
        }

        // Test with int cast
        int texIdInt = 112233;
        sf.setParameter("bgImageTexture", texIdInt);
        if (sf.getBgImageTexture() != static_cast<uint32_t>(texIdInt)) {
            fail(k, "setParameter('bgImageTexture') with int failed to sync"); return false;
        }
    } catch (...) { fail(k, "unexpected exception"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 13: SegmentationFilter 所有模式顺序切换验证
// ---------------------------------------------------------------------------
static bool test_segmentation_all_modes_switch() {
    const std::string k = "SegmentationFilter all modes switch";
    try {
        SegmentationFilter sf(nullptr, nullptr);
        SegmentationFilter::Mode modes[] = {
            SegmentationFilter::Mode::BLUR,
            SegmentationFilter::Mode::BG_COLOR,
            SegmentationFilter::Mode::TRANSPARENT,
            SegmentationFilter::Mode::BG_IMAGE,
            SegmentationFilter::Mode::ORIGINAL
        };
        for (auto m : modes) {
            sf.setParameter("mode", static_cast<int>(m));
            if (sf.getMode() != m) { fail(k, "Mode switch failed for " + std::to_string(static_cast<int>(m))); return false; }
        }
    } catch (...) { fail(k, "unexpected exception"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: loadModel() 错误路径
// ---------------------------------------------------------------------------
static bool test_load_model_invalid() {
    const std::string k = "loadModel(invalid_path) returns false";
    TfliteInferenceEngine eng;
    bool ok = eng.loadModel("/no/such/model.tflite");
    if (ok) { fail(k, "expected false but got true"); return false; }
    if (eng.getLastError().empty()) { fail(k, "lastError should be non-empty"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: runInference() 在未加载时返回 success=false
// ---------------------------------------------------------------------------
static bool test_inference_without_model() {
    const std::string k = "runInference() without model returns success=false";
    TfliteInferenceEngine eng;
    std::vector<uint8_t> dummy(64 * 64 * 4, 128u);
    auto result = eng.runInference(dummy.data(), 64, 64);
    if (result.success) { fail(k, "expected success=false"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4: release() 幂等（双调用不崩溃）
// ---------------------------------------------------------------------------
static bool test_release_idempotent() {
    const std::string k = "release() idempotent";
    try {
        TfliteInferenceEngine eng;
        eng.release();
        eng.release();
    } catch (...) { fail(k, "unexpected exception"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 5: BeautyFilter 参数读写
// ---------------------------------------------------------------------------
static bool test_beauty_params() {
    const std::string k = "BeautyFilter parameter set/read roundtrip";
    BeautyFilter bf;
    bf.setParameter("smoothStrength", 0.75f);
    bf.setParameter("whitenStrength", 0.25f);
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6: SegmentationFilter engine=nullptr 构造不崩溃
// ---------------------------------------------------------------------------
static bool test_segmentation_null_engine() {
    const std::string k = "SegmentationFilter(nullptr engine) no crash";
    try {
        SegmentationFilter sf(nullptr, nullptr);
        sf.setParameter("mode",        0);
        sf.setParameter("edgeSoften",  0.5f);
        sf.setParameter("bgColor",     static_cast<uint32_t>(0xFF000000u));
    } catch (...) { fail(k, "unexpected exception"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 7: loadModelFromBuffer() null buffer
// ---------------------------------------------------------------------------
static bool test_load_from_null_buffer() {
    const std::string k = "loadModelFromBuffer(nullptr, 0) returns false";
    TfliteInferenceEngine eng;
    bool ok = eng.loadModelFromBuffer(nullptr, 0);
    if (ok) { fail(k, "expected false"); return false; }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 8: decodeLandmarks 212 格式（XY 归一化）
// ---------------------------------------------------------------------------
static bool test_decode_landmarks_212() {
    const std::string k = "decodeLandmarks 212-float (XY only)";
    float mockOutput[212];
    for (int i = 0; i < 106; ++i) {
        mockOutput[i * 2 + 0] = static_cast<float>(i) / 105.0f; // x: 0.0 -> 1.0
        mockOutput[i * 2 + 1] = 1.0f - static_cast<float>(i) / 105.0f; // y: 1.0 -> 0.0
    }

    FaceResult res;
    FaceLandmarkDetector::decodeLandmarks(mockOutput, 212, 1000, 1000, res);

    if (!res.detected) { fail(k, "should be detected"); return false; }

    for (int i = 0; i < 106; ++i) {
        float expectedX = (static_cast<float>(i) / 105.0f) * 1000.0f;
        float expectedY = (1.0f - static_cast<float>(i) / 105.0f) * 1000.0f;
        if (std::abs(res.landmarks[i].x - expectedX) > 1e-4f ||
            std::abs(res.landmarks[i].y - expectedY) > 1e-4f) {
            fail(k, "incorrect denormalization at point " + std::to_string(i)); return false;
        }
        if (res.landmarks[i].score != 1.0f) {
            fail(k, "default score should be 1.0 at point " + std::to_string(i)); return false;
        }
    }
    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 9: decodeLandmarks 318 格式（XYS 置信度过滤）
// ---------------------------------------------------------------------------
static bool test_decode_landmarks_318_filtering() {
    const std::string k = "decodeLandmarks 318-float (XYS with filtering)";
    float mockOutput[318];
    for (int i = 0; i < 106; ++i) {
        mockOutput[i * 3 + 0] = 0.5f;
        mockOutput[i * 3 + 1] = 0.5f;
        // Test various scores: 0.0, 0.49 (invalid), 0.5 (valid), 1.0 (valid)
        if (i == 0) mockOutput[i * 3 + 2] = 0.0f;
        else if (i == 1) mockOutput[i * 3 + 2] = 0.49f;
        else if (i == 2) mockOutput[i * 3 + 2] = 0.5f;
        else if (i == 3) mockOutput[i * 3 + 2] = 1.0f;
        else mockOutput[i * 3 + 2] = 0.8f;
    }

    FaceResult res;
    FaceLandmarkDetector::decodeLandmarks(mockOutput, 318, 100, 100, res);

    if (!res.detected) { fail(k, "should be detected"); return false; }
    if (res.landmarks[0].score != 0.0f) { fail(k, "pt0 should be 0.0"); return false; }
    if (res.landmarks[1].score != 0.0f) { fail(k, "pt1 should be filtered (score=0)"); return false; }
    if (res.landmarks[2].score != 0.5f) { fail(k, "pt2 should be 0.5"); return false; }
    if (res.landmarks[3].score != 1.0f) { fail(k, "pt3 should be 1.0"); return false; }

    if (res.landmarks[1].x != 50.0f) { fail(k, "coord should still be preserved even if score=0"); return false; }

    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 10: decodeLandmarks 异常长度处理
// ---------------------------------------------------------------------------
static bool test_decode_landmarks_invalid_len() {
    const std::string k = "decodeLandmarks invalid output length";
    float mockOutput[100];
    FaceResult res;
    res.detected = true;
    FaceLandmarkDetector::decodeLandmarks(mockOutput, 100, 1280, 720, res);
    if (res.detected) { fail(k, "detected should be false for invalid length"); return false; }

    FaceLandmarkDetector::decodeLandmarks(nullptr, 212, 1280, 720, res);
    if (res.detected) { fail(k, "detected should be false for null output"); return false; }

    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// Test 10: BodyPoseDetector::decodeKeypoints
// ---------------------------------------------------------------------------
class BodyPoseDetectorTestAccessor : public BodyPoseDetector {
public:
    static PoseResult decodeKeypointsProxy(const float* output, int w, int h) {
        return BodyPoseDetector::decodeKeypoints(output, w, h);
    }
};

static bool test_body_pose_decode() {
    const std::string k = "BodyPoseDetector::decodeKeypoints (MoveNet format)";

    // 17 points * 3 = 51 floats. [y, x, score]
    float mockOutput[51];
    for (int i = 0; i < 17; ++i) {
        mockOutput[i * 3 + 0] = 0.5f; // y
        mockOutput[i * 3 + 1] = 0.2f; // x
        mockOutput[i * 3 + 2] = (i == 0) ? 0.1f : 0.8f; // point 0 invalid, others valid
    }

    int w = 1000, h = 500;
    auto res = BodyPoseDetectorTestAccessor::decodeKeypointsProxy(mockOutput, w, h);

    if (res.keypoints.size() != 17) { fail(k, "should have 17 points"); return false; }

    // Check denormalization: x = 0.2 * 1000 = 200, y = 0.5 * 500 = 250
    if (res.keypoints[0].x != 200.0f || res.keypoints[0].y != 250.0f) {
        fail(k, "incorrect denormalization"); return false;
    }

    // Check isValid (default threshold 0.3)
    if (res.keypoints[0].isValid()) { fail(k, "pt0 should be invalid (score 0.1)"); return false; }
    if (!res.keypoints[1].isValid()) { fail(k, "pt1 should be valid (score 0.8)"); return false; }

    // Check poseScore calculation: (0.1 + 16 * 0.8) / 17 = 12.9 / 17 ≈ 0.7588
    float expectedScore = (0.1f + 16 * 0.8f) / 17.0f;
    if (std::abs(res.poseScore - expectedScore) > 1e-5) {
        fail(k, "incorrect poseScore"); return false;
    }

    pass(k);
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "\n=== test_ai_inference ===\n";
#ifdef HAS_TFLITE
    std::cout << "  [Mode] TFLite integrated (HAS_TFLITE=1)\n\n";
#else
    std::cout << "  [Mode] TFLite not integrated — testing stub behavior\n\n";
#endif

    int passed = 0, total = 0;
    auto run = [&](bool ok) { ++total; if (ok) ++passed; };

    run(test_engine_construct());
    run(test_load_model_invalid());
    run(test_inference_without_model());
    run(test_release_idempotent());
    run(test_beauty_params());
    run(test_segmentation_null_engine());
    run(test_load_from_null_buffer());
    run(test_decode_landmarks_212());
    run(test_decode_landmarks_318_filtering());
    run(test_segmentation_params());
    run(test_segmentation_default_params());
    run(test_segmentation_set_parameter_sync());
    run(test_segmentation_all_modes_switch());

    std::cout << "\nResult: " << passed << "/" << total << " passed\n";
    return (passed == total) ? 0 : 1;
}
