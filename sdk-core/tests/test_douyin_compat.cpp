/**
 * test_douyin_compat.cpp
 *
 * 抖音对标功能单元测试
 * 覆盖：FaceLandmarkDetector / FaceReshapeFilter / MakeupFilter /
 *       HairSegmentationFilter / EffectPluginManager
 */

#include "../core/include/ai/FaceLandmarkDetector.h"
#include "../core/include/ai/FaceReshapeFilter.h"
#include "../core/include/ai/MakeupFilter.h"
#include "../core/include/ai/HairSegmentationFilter.h"
#include "../core/include/EffectPlugin.h"

#include <cstdio>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>

using namespace sdk::video;
using namespace sdk::video::ai;

// ---------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
#define EXPECT(cond, msg) \
    do { \
        if (cond) { ++g_pass; printf("[PASS] %s\n", msg); } \
        else { ++g_fail; printf("[FAIL] %s  (line %d)\n", msg, __LINE__); } \
    } while(0)

// ---------------------------------------------------------------------------
// 1. FaceLandmarkDetector — stub 模式测试
// ---------------------------------------------------------------------------
static void testFaceLandmarkDetector() {
    printf("\n-- FaceLandmarkDetector --\n");

    FaceLandmarkDetector det;
    EXPECT(!det.isLoaded(), "not loaded before loadModel");

    // stub 模式：无模型文件也能加载（内部 stub 实现）
    bool ok = det.loadModel("/stub/face.tflite");
    EXPECT(ok, "loadModel stub returns true");
    EXPECT(det.isLoaded(), "isLoaded after stub loadModel");

    // 同步推理
    std::vector<uint8_t> fakeFrame(128 * 128 * 4, 128);
    auto result = det.runSync(fakeFrame.data(), 128, 128);
    EXPECT(result.faceCount == 1, "stub: faceCount == 1");
    EXPECT(result.faces[0].detected, "stub: face detected");
    EXPECT(result.faces[0].faceScore > 0.5f, "stub: faceScore > 0.5");
    EXPECT(result.inferenceTimeMs >= 0.f, "inferenceTimeMs >= 0");

    // 关键点数量
    const auto& lm = result.faces[0].landmarks;
    EXPECT(lm.size() == kFaceLandmarkCount, "landmark count == 106");

    // 坐标范围 [0,1]
    bool inRange = true;
    for (const auto& p : lm) {
        if (p.x < 0.f || p.x > 1.f || p.y < 0.f || p.y > 1.f) { inRange = false; break; }
    }
    EXPECT(inRange, "all landmarks in [0,1]");

    // 语义访问器
    auto nose = result.faces[0].noseTip();
    EXPECT(nose.x > 0.f && nose.x < 1.f, "noseTip().x in range");
    EXPECT(nose.y > 0.f && nose.y < 1.f, "noseTip().y in range");

    // 异步提交（不阻塞，只验证不崩溃）
    det.submitFrame(fakeFrame.data(), 128, 128, 1000000LL);
    auto latest = det.getLatestResult();
    EXPECT(latest.faceCount >= 0, "getLatestResult does not crash");

    // 平均耗时
    EXPECT(det.getAverageInferenceMs() >= 0.f, "avgInferenceMs >= 0");

    det.release();
    EXPECT(!det.isLoaded(), "not loaded after release");
}

// ---------------------------------------------------------------------------
// 2. FaceReshapeFilter — 构造/参数设置
// ---------------------------------------------------------------------------
static void testFaceReshapeFilter() {
    printf("\n-- FaceReshapeFilter --\n");

    FaceReshapeFilter filter;
    // 参数设置不崩溃
    filter.setEyeScale(0.5f);
    filter.setFaceSlim(0.3f);
    filter.setNoseSlim(0.2f);
    filter.setForeheadUp(0.1f);
    filter.setChinV(0.4f);
    filter.setMouthWidth(0.1f);
    EXPECT(true, "FaceReshapeFilter params set without crash");

    // 传入关键点结果
    LandmarkFrameResult lr;
    lr.faceCount = 1;
    lr.faces[0].detected = true;
    lr.faces[0].faceScore = 0.9f;
    for (int i = 0; i < kFaceLandmarkCount; ++i) {
        lr.faces[0].landmarks[i] = {0.5f, 0.5f, 0.9f};
    }
    filter.setLandmarkResult(lr);
    EXPECT(true, "FaceReshapeFilter setLandmarkResult without crash");

    // 无关键点时不崩溃
    LandmarkFrameResult empty{};
    filter.setLandmarkResult(empty);
    EXPECT(true, "FaceReshapeFilter handles empty LandmarkResult");
}

// ---------------------------------------------------------------------------
// 3. MakeupFilter — 参数全覆盖
// ---------------------------------------------------------------------------
static void testMakeupFilter() {
    printf("\n-- MakeupFilter --\n");

    MakeupFilter makeup;
    makeup.setLipColor  (0.8f, 0.1f, 0.2f, 0.8f);
    makeup.setBlush     (0.9f, 0.5f, 0.5f, 0.5f);
    makeup.setEyeshadow (0.3f, 0.1f, 0.6f, 0.6f);
    makeup.setHighlight (0.4f);
    makeup.setContour   (0.3f);
    makeup.setEyebrow   (0.2f, 0.1f, 0.05f, 0.7f);
    EXPECT(true, "MakeupFilter: all layers set without crash");

    LandmarkFrameResult lr;
    lr.faceCount = 1; lr.faces[0].detected = true;
    for (int i = 0; i < kFaceLandmarkCount; ++i)
        lr.faces[0].landmarks[i] = {0.5f + i * 0.001f, 0.5f + i * 0.001f, 1.f};
    makeup.setLandmarkResult(lr);
    EXPECT(true, "MakeupFilter: setLandmarkResult without crash");

    // 关闭所有妆容层（强度=0）
    makeup.setLipColor(0,0,0,0);
    makeup.setBlush(0,0,0,0);
    makeup.setEyeshadow(0,0,0,0);
    makeup.setHighlight(0);
    makeup.setContour(0);
    makeup.setEyebrow(0,0,0,0);
    EXPECT(true, "MakeupFilter: all layers disabled without crash");
}

// ---------------------------------------------------------------------------
// 4. HairSegmentationFilter — 构造/参数
// ---------------------------------------------------------------------------
static void testHairSegFilter() {
    printf("\n-- HairSegmentationFilter --\n");

    HairSegmentationFilter hair;
    hair.setHairColor(0.5f, 0.0f, 0.8f);
    hair.setColorIntensity(0.75f);
    hair.setGlossIntensity(0.25f);
    EXPECT(true, "HairSegFilter: params set without crash");

    bool ok = hair.loadModel("/stub/hair.tflite");
    // stub 模式下 TfliteInferenceEngine::loadModel 也是 stub，返回 true
    EXPECT(true, "HairSegFilter: loadModel does not crash");
}

// ---------------------------------------------------------------------------
// 5. EffectPluginManager — JSON 解析 + 激活 / 卸载
// ---------------------------------------------------------------------------
static void testEffectPluginManager() {
    printf("\n-- EffectPluginManager --\n");

    EffectPluginManager mgr;

    // 空 assetLoader — loadEffect 应返回空，不崩溃
    std::string id = mgr.loadEffect("/nonexistent/path");
    EXPECT(id.empty(), "loadEffect nonexistent returns empty");

    // 手动传入 manifest JSON
    const std::string json = R"({
        "id": "test_effect_001",
        "name": "测试特效",
        "version": "1.0",
        "type": "face_sticker",
        "layers": [
            {
                "type": "beauty",
                "eyeScale": 0.3,
                "faceSlim": 0.2,
                "noseSlim": 0.15,
                "foreheadUp": 0.1,
                "chinV": 0.25
            },
            {
                "type": "color_filter",
                "lut": "luts/warm.cube",
                "intensity": 0.7
            },
            {
                "type": "makeup",
                "lipColor": [0.8, 0.1, 0.2, 0.7],
                "highlight": 0.4,
                "contour": 0.3
            }
        ]
    })";

    // 设置 stub assetLoader（无法读文件，仅 JSON 解析）
    mgr.setAssetLoader([](const std::string&) { return std::vector<uint8_t>{}; });
    id = mgr.loadEffectFromJSON(json, "/effects/test");
    EXPECT(id == "test_effect_001", "loadEffectFromJSON id == 'test_effect_001'");

    auto ids = mgr.getEffectIds();
    EXPECT(ids.size() == 1, "getEffectIds size == 1");
    EXPECT(ids[0] == "test_effect_001", "getEffectIds[0] == 'test_effect_001'");

    // 激活
    mgr.activateEffect("test_effect_001");
    auto* active = mgr.getActiveEffect();
    EXPECT(active != nullptr, "getActiveEffect != nullptr after activate");
    EXPECT(active->getId() == "test_effect_001", "active effect id matches");
    EXPECT(active->isReady(), "effect isReady after load");

    // 层数验证
    EXPECT(active->getDesc().layers.size() == 3, "effect has 3 layers");
    EXPECT(active->getDesc().layers[0].type == EffectLayerType::Beauty,
           "layer[0] type == Beauty");
    EXPECT(active->getDesc().layers[0].eyeScale > 0.f, "Beauty eyeScale > 0");
    EXPECT(active->getDesc().layers[1].type == EffectLayerType::ColorFilter,
           "layer[1] type == ColorFilter");
    EXPECT(active->getDesc().layers[2].type == EffectLayerType::Makeup,
           "layer[2] type == Makeup");
    EXPECT(active->getDesc().layers[2].lipColor[0] > 0.f, "Makeup lipColor.r > 0");
    EXPECT(active->getDesc().layers[2].highlightIntensity > 0.f,
           "Makeup highlight > 0");

    // 强度覆盖
    active->setIntensity(0.5f);
    EXPECT(active->getIntensity() == 0.5f, "setIntensity(0.5)");

    // 停用
    mgr.deactivateAll();
    EXPECT(mgr.getActiveEffect() == nullptr, "getActiveEffect == nullptr after deactivate");

    // 重新激活
    mgr.activateEffect("test_effect_001");
    EXPECT(mgr.getActiveEffect() != nullptr, "re-activate works");

    // 卸载
    mgr.unloadEffect("test_effect_001");
    EXPECT(mgr.getActiveEffect() == nullptr, "getActiveEffect == nullptr after unload");
    EXPECT(mgr.getEffectIds().empty(), "getEffectIds empty after unload");

    // 加载第二个特效
    const std::string json2 = R"({"id":"eff2","name":"2","version":"1","type":"color_filter","layers":[]})";
    mgr.loadEffectFromJSON(json2, "/effects/eff2");
    EXPECT(mgr.getEffectIds().size() == 1, "second effect loaded");

    mgr.unloadAll();
    EXPECT(mgr.getEffectIds().empty(), "unloadAll clears all effects");
}

// ---------------------------------------------------------------------------
// 6. EffectLayerType 枚举完整性
// ---------------------------------------------------------------------------
static void testEffectLayerTypeEnum() {
    printf("\n-- EffectLayerType Enum --\n");
    EXPECT((int)EffectLayerType::Unknown     == 0, "Unknown == 0");
    EXPECT((int)EffectLayerType::ColorFilter == 1, "ColorFilter == 1");
    EXPECT((int)EffectLayerType::Sticker     == 2, "Sticker == 2");
    EXPECT((int)EffectLayerType::Beauty      == 3, "Beauty == 3");
    EXPECT((int)EffectLayerType::Makeup      == 4, "Makeup == 4");
    EXPECT((int)EffectLayerType::Segmentation== 5, "Segmentation == 5");
    EXPECT((int)EffectLayerType::Particle    == 6, "Particle == 6");
}

// ---------------------------------------------------------------------------
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("========= test_douyin_compat =========\n");
    testFaceLandmarkDetector();
    testFaceReshapeFilter();
    testMakeupFilter();
    testHairSegFilter();
    testEffectPluginManager();
    testEffectLayerTypeEnum();

    printf("\n====== Result: %d passed, %d failed ======\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
