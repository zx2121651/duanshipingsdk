/**
 * test_remaining_gaps.cpp
 *
 * 剩余功能差距补齐单元测试：
 *   1-6:   FaceMorphFilter — 参数 setter/getter/reset
 *   7-11:  BodyEffectFilter — 参数 setter/getter/reset/updatePose
 *   12-18: SmartCropController — 宽高比/平滑/compute/reset
 *   19-25: HistogramAnalyzer — analyze/stats/过曝/欠曝/归一化/波形
 *   26-32: DepthEstimator — estimate(CPU)/buildBokehMask/resize/colormap
 *   33-37: SplitScreenCompositor — layout/setSlot/setCells/slotCount
 */

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <memory>
#include <fstream>

#ifdef _WIN32
#   include <direct.h>
#else
#   include <unistd.h>
#endif

#include "../core/include/ai/FaceMorphFilter.h"
#include "../core/include/ai/BodyEffectFilter.h"
#include "../core/include/timeline/SmartCropController.h"
#include "../core/include/timeline/HistogramAnalyzer.h"
#include "../core/include/ai/DepthEstimator.h"
#include "../core/include/timeline/SplitScreenCompositor.h"
#include "../core/include/EffectPackageUpdater.h"

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
#define ASSERT_NEAR(a, b, eps, name) \
    do { float _d=(float)(a)-(float)(b); if(_d<0)_d=-_d; \
         if(_d>(eps)) fail(name,std::to_string(a)+" != "+std::to_string(b)); \
         else pass(name); } while(0)

// ---------------------------------------------------------------------------
// Tests 1-6: FaceMorphFilter
// ---------------------------------------------------------------------------
static void testFaceMorphFilter() {
    std::cout << "\n=== FaceMorphFilter ===\n";

    FaceMorphFilter f;

    // T1: default all strengths 0
    ASSERT_NEAR(f.getStrength(FaceMorphFilter::Effect::SLIM_FACE), 0.f, 0.001f,
                "T1: default strength 0");

    // T2: setStrength clamped to [0,1]
    f.setStrength(FaceMorphFilter::Effect::SLIM_FACE, 0.5f);
    ASSERT_NEAR(f.getStrength(FaceMorphFilter::Effect::SLIM_FACE), 0.5f, 0.001f,
                "T2: setStrength 0.5");

    f.setStrength(FaceMorphFilter::Effect::BIG_EYES, 2.0f);  // clamp to 1
    ASSERT_NEAR(f.getStrength(FaceMorphFilter::Effect::BIG_EYES), 1.0f, 0.001f,
                "T3: clamp to 1.0");

    f.setStrength(FaceMorphFilter::Effect::SLIM_JAW, -0.5f); // clamp to 0
    ASSERT_NEAR(f.getStrength(FaceMorphFilter::Effect::SLIM_JAW), 0.f, 0.001f,
                "T4: clamp to 0.0");

    // T5: resetAll
    f.resetAll();
    ASSERT_NEAR(f.getStrength(FaceMorphFilter::Effect::SLIM_FACE), 0.f, 0.001f,
                "T5: resetAll clears strengths");

    // T6: updateLandmarks no crash (without GL context)
    FaceResult face;
    face.detected = false;
    f.updateLandmarks(face);
    ASSERT_TRUE(true, "T6: updateLandmarks no crash");
}

// ---------------------------------------------------------------------------
// Tests 7-11: BodyEffectFilter
// ---------------------------------------------------------------------------
static void testBodyEffectFilter() {
    std::cout << "\n=== BodyEffectFilter ===\n";

    BodyEffectFilter b;

    // T7: default 0
    ASSERT_NEAR(b.getStrength(BodyEffectFilter::Effect::SLIM_BODY), 0.f, 0.001f,
                "T7: default SLIM_BODY=0");

    // T8: set/get
    b.setStrength(BodyEffectFilter::Effect::LONG_LEGS, 0.7f);
    ASSERT_NEAR(b.getStrength(BodyEffectFilter::Effect::LONG_LEGS), 0.7f, 0.001f,
                "T8: LONG_LEGS set 0.7");

    // T9: clamp
    b.setStrength(BodyEffectFilter::Effect::SMALL_HEAD, 1.5f);
    ASSERT_NEAR(b.getStrength(BodyEffectFilter::Effect::SMALL_HEAD), 1.f, 0.001f,
                "T9: clamp to 1.0");

    // T10: resetAll
    b.resetAll();
    ASSERT_NEAR(b.getStrength(BodyEffectFilter::Effect::LONG_LEGS), 0.f, 0.001f,
                "T10: resetAll clears");

    // T11: updatePose no crash
    BodyPoseResult pose;
    pose.detected = false;
    b.updatePose(pose);
    ASSERT_TRUE(true, "T11: updatePose no crash");
}

// ---------------------------------------------------------------------------
// Tests 12-18: SmartCropController
// ---------------------------------------------------------------------------
static void testSmartCropController() {
    std::cout << "\n=== SmartCropController ===\n";

    SmartCropController ctrl;

    // T12: default aspect ratio set without crash
    ctrl.setAspectRatio(SmartCropController::AspectRatio::RATIO_9_16);
    ASSERT_TRUE(true, "T12: setAspectRatio no crash");

    // T13: custom ratio
    ctrl.setCustomAspectRatio(16.f, 9.f);
    ASSERT_TRUE(true, "T13: setCustomAspectRatio no crash");

    // T14: compute with no subjects → valid crop
    auto crop0 = ctrl.compute(1080, 1920);
    ASSERT_TRUE(crop0.valid(), "T14: compute with no subjects → valid rect");

    // T15: crop w,h > 0 and <= 1
    ASSERT_TRUE(crop0.w > 0.f && crop0.w <= 1.f, "T15: crop.w in (0,1]");
    ASSERT_TRUE(crop0.h > 0.f && crop0.h <= 1.f, "T15: crop.h in (0,1]");

    // T16: updateDetection → compute shifts toward subject
    ctrl.reset();
    ctrl.setAspectRatio(SmartCropController::AspectRatio::RATIO_9_16);
    ctrl.setSmoothFactor(0.f);  // instant response
    SubjectRegion sub;
    sub.rect = {0.1f, 0.1f, 0.2f, 0.3f};
    sub.type = SubjectRegion::Type::FACE;
    sub.confidence = 0.95f;
    ctrl.updateDetection({sub});
    auto crop1 = ctrl.compute(1080, 1920);
    ASSERT_TRUE(crop1.valid(), "T16: compute with face subject → valid");

    // T17: smoothedCenter in (0,1)
    float cx = ctrl.smoothedCenterX(), cy = ctrl.smoothedCenterY();
    ASSERT_TRUE(cx >= 0.f && cx <= 1.f, "T17: smoothedCenterX in [0,1]");
    ASSERT_TRUE(cy >= 0.f && cy <= 1.f, "T17: smoothedCenterY in [0,1]");

    // T18: reset clears smooth state
    ctrl.reset();
    auto crop2 = ctrl.compute(1080, 1920);
    ASSERT_TRUE(crop2.valid(), "T18: compute after reset still valid");
}

// ---------------------------------------------------------------------------
// Tests 19-25: HistogramAnalyzer
// ---------------------------------------------------------------------------
static void makeTestFrame(std::vector<uint8_t>& buf, int w, int h,
                           uint8_t r, uint8_t g, uint8_t bv) {
    buf.resize(w * h * 4);
    for (int i = 0; i < w*h; ++i) {
        buf[i*4+0]=r; buf[i*4+1]=g; buf[i*4+2]=bv; buf[i*4+3]=255;
    }
}

static void testHistogramAnalyzer() {
    std::cout << "\n=== HistogramAnalyzer ===\n";

    HistogramAnalyzer ha;

    // T19: solid gray frame
    std::vector<uint8_t> frame;
    makeTestFrame(frame, 64, 64, 128, 128, 128);
    ha.analyze(frame.data(), 64, 64);
    ASSERT_TRUE(ha.isValid(), "T19: isValid after analyze");

    // T20: luma mean ≈ 128 for neutral gray (BT.601: 0.299*128+0.587*128+0.114*128=128)
    ASSERT_NEAR(ha.lumaMean(), 128.f, 5.f, "T20: gray luma mean ≈ 128");

    // T21: totalPixels
    ASSERT_TRUE(ha.totalPixels() == 64*64, "T21: totalPixels == 4096");

    // T22: histogram peak at 128
    const auto& hist = ha.getLumaHistogram();
    ASSERT_TRUE(hist[128] == (uint32_t)(64*64), "T22: all pixels at bin 128");

    // T23: not overexposed for gray 128
    ASSERT_TRUE(!ha.isOverexposed(0.05f, 245), "T23: 128-gray not overexposed");

    // T24: overexposed for white frame
    makeTestFrame(frame, 32, 32, 255, 255, 255);
    ha.analyze(frame.data(), 32, 32);
    ASSERT_TRUE(ha.isOverexposed(0.05f, 245), "T24: white frame overexposed");

    // T25: normalized histogram peak == 1.0
    auto norm = ha.getNormalizedHistogram(ha.getLumaHistogram());
    float pk = *std::max_element(norm.begin(), norm.end());
    ASSERT_NEAR(pk, 1.f, 0.01f, "T25: normalized hist peak == 1.0");
}

// ---------------------------------------------------------------------------
// Tests 26-32: DepthEstimator
// ---------------------------------------------------------------------------
static void testDepthEstimator() {
    std::cout << "\n=== DepthEstimator ===\n";

    DepthEstimator de;

    // T26: no backend → gradient fallback
    ASSERT_TRUE(!de.hasBackend(), "T26: no backend initially");

    // T27: estimate small frame
    std::vector<uint8_t> frame(32 * 32 * 4, 128);
    // Add gradient to simulate texture
    for (int y=0;y<32;++y)
        for (int x=0;x<32;++x) {
            frame[(y*32+x)*4] = (uint8_t)(x * 8);
            frame[(y*32+x)*4+1] = (uint8_t)(y * 8);
        }
    auto dm = de.estimate(frame.data(), 32, 32);
    ASSERT_TRUE(dm.valid, "T27: estimate returns valid DepthMap");
    ASSERT_TRUE(dm.width == 32 && dm.height == 32, "T27: output size matches");

    // T28: depth values in [0,1]
    bool allInRange = true;
    for (float v : dm.data) if (v<0.f||v>1.f) { allInRange=false; break; }
    ASSERT_TRUE(allInRange, "T28: all depth values in [0,1]");

    // T29: resize
    auto dm2 = DepthEstimator::resize(dm, 16, 16);
    ASSERT_TRUE(dm2.width==16 && dm2.height==16 && dm2.valid, "T29: resize 32→16");

    // T30: buildBokehMask
    auto mask = DepthEstimator::buildBokehMask(dm, 0.5f, 0.2f);
    ASSERT_TRUE((int)mask.size() == dm.width*dm.height, "T30: mask size == depth size");
    bool maskInRange = true;
    for (float v : mask) if (v<0.f||v>1.f) { maskInRange=false; break; }
    ASSERT_TRUE(maskInRange, "T30: mask values in [0,1]");

    // T31: toColormap
    auto cmap = DepthEstimator::toColormap(dm);
    ASSERT_TRUE((int)cmap.size() == dm.width*dm.height*4, "T31: colormap RGBA size");

    // T32: stats
    float mn = de.minDepth(dm), mx = de.maxDepth(dm), mean = de.meanDepth(dm);
    ASSERT_TRUE(mn >= 0.f && mx <= 1.f && mean >= mn && mean <= mx,
                "T32: depth stats consistent");
}

// ---------------------------------------------------------------------------
// Tests 33-37: SplitScreenCompositor
// ---------------------------------------------------------------------------
static void testSplitScreenCompositor() {
    std::cout << "\n=== SplitScreenCompositor ===\n";

    SplitScreenCompositor comp;

    // T33: default layout
    comp.setLayout(SplitScreenCompositor::Layout::GRID_2x2);
    ASSERT_TRUE(comp.getLayout() == SplitScreenCompositor::Layout::GRID_2x2,
                "T33: layout set to GRID_2x2");

    // T34: slotCount matches layout
    ASSERT_TRUE(comp.slotCount() == 4, "T34: 2x2 has 4 slots");

    // T35: setSlot no crash
    comp.setSlot(0, 42);
    comp.setSlot(1, 43);
    comp.setSlot(2, 0);
    comp.setSlot(3, 44);
    ASSERT_TRUE(true, "T35: setSlot no crash");

    // T36: setCells custom layout
    std::vector<SplitScreenCompositor::Cell> cells = {
        {0.f,  0.f, 0.5f, 1.f, 10},
        {0.5f, 0.f, 0.5f, 1.f, 11},
    };
    comp.setCells(cells);
    ASSERT_TRUE(comp.slotCount() == 2, "T36: custom 2-cell layout");
    ASSERT_TRUE(comp.getLayout() == SplitScreenCompositor::Layout::CUSTOM,
                "T36: layout is CUSTOM after setCells");

    // T37: setGapPx / setBackgroundColor / setBorder no crash
    comp.setGapPx(4);
    comp.setBackgroundColor(0xFF111111u);
    comp.setBorder(2, 0xFFFFFFFFu);
    ASSERT_TRUE(true, "T37: setGapPx/setBgColor/setBorder no crash");
}

// ---------------------------------------------------------------------------
// Tests 38-44: EffectPackageUpdater Cancel Protection
// ---------------------------------------------------------------------------
class MockHttpClientForCancel : public IHttpClient {
public:
    int get(const std::string& url, std::string& body) override {
        body = "{\"packages\":[{\"id\":\"pkg_test\",\"name\":\"Test\",\"version\":\"1.0.0\",\"download_url\":\"http://test.zip\",\"checksum\":\"\",\"checksum_type\":\"\",\"size_bytes\":100,\"category\":\"sticker\"}]}";
        return 200;
    }

    int download(const std::string& url,
                 const std::string& destPath,
                 int64_t            resumeBytes,
                 std::function<void(int64_t, int64_t)> onProgress) override {
        // 模拟下载过程，调用一次 progress 回调
        if (onProgress) {
            onProgress(50, 100);
        }
        // 模拟外部触发了 cancel
        if (cancelCallback) {
            cancelCallback();
        }
        // 调用第二次 progress，测试 cancel 后状态不被覆盖
        if (onProgress) {
            onProgress(100, 100);
        }
        // 模拟写入临时文件
        std::ofstream f(destPath, std::ios::binary);
        f.write("mock data", 9);
        f.close();
        return 200;
    }

    std::function<void()> cancelCallback;
};

static void testEffectPackageUpdaterCancel() {
    std::cout << "\n=== EffectPackageUpdater Cancel Protection ===\n";

    // 创建安装目录
    std::string testInstallDir = "test_effects_dir";
    auto updater = std::make_shared<EffectPackageUpdater>(testInstallDir);
    auto client = std::make_shared<MockHttpClientForCancel>();
    updater->setHttpClient(client);
    updater->setManifestUrl("http://manifest.json");

    // 检查更新
    auto updates = updater->checkForUpdatesSync();
    ASSERT_TRUE(updates.size() == 1, "T38: find remote package");
    ASSERT_TRUE(updates[0].id == "pkg_test", "T39: package id matches");

    client->cancelCallback = [&]() {
        updater->cancelDownload("pkg_test");
    };

    bool callbackInvoked = false;
    bool downloadSuccess = false;
    std::string errStr;

    updater->downloadPackage("pkg_test", 
        [](const DownloadProgress& dp) {
            // progress 回调
        },
        [&](const std::string& packageId, bool success, const std::string& installPath, const std::string& error) {
            callbackInvoked = true;
            downloadSuccess = success;
            errStr = error;
        }
    );

    ASSERT_TRUE(callbackInvoked, "T40: complete callback invoked");
    ASSERT_TRUE(!downloadSuccess, "T41: download was not marked success");
    ASSERT_TRUE(errStr == "Download cancelled", "T42: error message matches 'Download cancelled'");

    // 校验临时文件是否被删除
    std::string tmpPath = testInstallDir + "/pkg_test.zip.tmp";
    std::ifstream chk(tmpPath);
    ASSERT_TRUE(!chk.is_open(), "T43: temporary file cleanup successful");

    // 校验本地 Manifest 没有被写入这个包
    ASSERT_TRUE(!updater->isInstalled("pkg_test"), "T44: package was not registered in local manifest");

    // 清理
    updater->uninstallAll();
    // 移除测试文件夹和临时文件
    std::remove((testInstallDir + "/manifest.json").c_str());
#ifdef _WIN32
    _rmdir(testInstallDir.c_str());
#else
    rmdir(testInstallDir.c_str());
#endif
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== test_remaining_gaps ===\n";

    testFaceMorphFilter();
    testBodyEffectFilter();
    testSmartCropController();
    testHistogramAnalyzer();
    testDepthEstimator();
    testSplitScreenCompositor();
    testEffectPackageUpdaterCancel();

    std::cout << "\n\033[32mAll remaining gap tests PASSED.\033[0m\n";
    return 0;
}
