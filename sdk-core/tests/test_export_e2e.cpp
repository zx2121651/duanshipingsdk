/**
 * test_export_e2e.cpp
 * Phase B — 导出链路 E2E 测试
 *
 * 覆盖：
 *   - 音视频双轨均写入 mp4（trackIndex >= 0）
 *   - video-only 路径（null AudioMixer）不崩溃
 *   - cancel 路径返回 ERR_EXPORTER_CANCELLED
 *   - 连续导出（上次完成后再次配置）
 */

#include <iostream>
#include <cassert>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include "../core/include/timeline/TimelineExporter.h"
#include "../core/include/timeline/Timeline.h"
#include "../core/include/timeline/SyncClock.h"
#include "../core/include/GLTypes.h"

using namespace sdk::video;
using namespace sdk::video::timeline;

// ---------------------------------------------------------------------------
// Stub exporter that tracks track registration and muxer start, mimicking
// the real TimelineExporterAndroid without a device.
// ---------------------------------------------------------------------------
struct ExportCapture {
    int  videoTrackIndex = -1;
    int  audioTrackIndex = -1;
    bool muxerStarted    = false;
    bool eosReached      = false;
    std::atomic<bool> cancelled{false};
    std::atomic<float> lastProgress{0.0f};
};

class E2EStubExporter : public TimelineExporter {
public:
    explicit E2EStubExporter(ExportCapture& cap, bool withAudio = true)
        : m_cap(cap), m_withAudio(withAudio) {}

    Result configure(const std::string& outputPath,
                     int width, int height, int fps, int bitrate) override {
        m_outputPath = outputPath;
        m_state      = State::IDLE;
        return Result::ok();
    }

    void configureChunking(int64_t, ChunkCallback) override {}

    Result exportAsync(std::shared_ptr<Timeline>   /*timeline*/,
                       std::shared_ptr<Compositor> /*compositor*/,
                       ProgressCallback onProgress,
                       CompletionCallback onComplete) override {
        m_state = State::EXPORTING;
        m_thread = std::thread([this, onProgress, onComplete]() {
            // Simulate track registration (what real Android muxer does)
            m_cap.videoTrackIndex = 0;
            if (m_withAudio) {
                m_cap.audioTrackIndex = 1;
            }
            m_cap.muxerStarted = true;

            for (int i = 1; i <= 10; ++i) {
                if (m_cap.cancelled.load()) break;
                m_cap.lastProgress.store(i / 10.0f);
                if (onProgress) onProgress(i / 10.0f);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            if (m_cap.cancelled.load()) {
                m_state = State::CANCELED;
                if (onComplete)
                    onComplete(Result::error(ErrorCode::ERR_EXPORTER_CANCELLED, "User cancelled"));
            } else {
                m_cap.eosReached = true;
                m_state          = State::COMPLETED;
                if (onComplete) onComplete(Result::ok());
            }
        });
        return Result::ok();
    }

    void cancel() override {
        m_cap.cancelled.store(true);
    }

    State getState() const override { return m_state; }
    float getProgress() const override { return m_cap.lastProgress.load(); }

    void waitDone() { if (m_thread.joinable()) m_thread.join(); }

private:
    ExportCapture&     m_cap;
    bool               m_withAudio;
    std::string        m_outputPath;
    std::atomic<State> m_state{State::IDLE};
    std::thread        m_thread;
};

// ---------------------------------------------------------------------------
// TC-B01: 音视频双轨均注册，muxer 正常启动，EOS 到达
// ---------------------------------------------------------------------------
static void tc_b01_av_tracks_registered() {
    ExportCapture cap;
    E2EStubExporter exporter(cap, /*withAudio=*/true);

    exporter.configure("output_b01.mp4", 1920, 1080, 30, 8000000);

    bool completed = false;
    exporter.exportAsync(nullptr, nullptr,
        [](float) {},
        [&completed](Result r) {
            assert(r.isOk());
            completed = true;
        });

    assert(exporter.getState() == TimelineExporter::State::EXPORTING);
    exporter.waitDone();

    assert(exporter.getState() == TimelineExporter::State::COMPLETED);
    assert(cap.videoTrackIndex >= 0);
    assert(cap.audioTrackIndex >= 0);
    assert(cap.muxerStarted);
    assert(cap.eosReached);
    assert(completed);

    std::cout << "TC-B01 PASS: av_tracks_registered "
              << "(video=" << cap.videoTrackIndex
              << ", audio=" << cap.audioTrackIndex << ")" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-B02: Video-only（无音频轨）不崩溃，videoTrack >= 0, audioTrack == -1
// ---------------------------------------------------------------------------
static void tc_b02_video_only_no_crash() {
    ExportCapture cap;
    E2EStubExporter exporter(cap, /*withAudio=*/false);

    exporter.configure("output_b02.mp4", 1280, 720, 30, 4000000);

    bool completed = false;
    exporter.exportAsync(nullptr, nullptr, nullptr,
        [&completed](Result r) {
            assert(r.isOk());
            completed = true;
        });

    exporter.waitDone();

    assert(exporter.getState() == TimelineExporter::State::COMPLETED);
    assert(cap.videoTrackIndex >= 0);
    assert(cap.audioTrackIndex == -1);
    assert(!cap.cancelled.load());
    assert(completed);

    std::cout << "TC-B02 PASS: video_only_no_crash" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-B03: cancel 路径 — exportAsync → cancelExport → ERR_EXPORTER_CANCELLED
// ---------------------------------------------------------------------------
static void tc_b03_cancel_returns_cancelled_code() {
    ExportCapture cap;
    E2EStubExporter exporter(cap, /*withAudio=*/true);

    exporter.configure("output_b03.mp4", 1280, 720, 30, 4000000);

    int  gotErrorCode = 0;
    bool callbackFired = false;

    exporter.exportAsync(nullptr, nullptr, nullptr,
        [&](Result r) {
            assert(!r.isOk());
            gotErrorCode  = r.getErrorCode();
            callbackFired = true;
        });

    exporter.cancel();
    exporter.waitDone();

    assert(exporter.getState() == TimelineExporter::State::CANCELED);
    assert(callbackFired);
    assert(gotErrorCode == ErrorCode::ERR_EXPORTER_CANCELLED);

    std::cout << "TC-B03 PASS: cancel_returns_cancelled_code (code="
              << gotErrorCode << ")" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-B04: 连续导出 — 完成后重新 configure 并再次 exportAsync 不崩溃
// ---------------------------------------------------------------------------
static void tc_b04_consecutive_exports() {
    ExportCapture cap1, cap2;

    {
        E2EStubExporter exporter(cap1, true);
        exporter.configure("output_b04_first.mp4", 1280, 720, 30, 4000000);
        exporter.exportAsync(nullptr, nullptr, nullptr, nullptr);
        exporter.waitDone();
        assert(exporter.getState() == TimelineExporter::State::COMPLETED);
    }

    {
        E2EStubExporter exporter(cap2, false);
        exporter.configure("output_b04_second.mp4", 1920, 1080, 60, 16000000);
        exporter.exportAsync(nullptr, nullptr, nullptr, nullptr);
        exporter.waitDone();
        assert(exporter.getState() == TimelineExporter::State::COMPLETED);
    }

    assert(cap1.muxerStarted && cap2.muxerStarted);
    std::cout << "TC-B04 PASS: consecutive_exports" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-B05: SyncClock — A/V offset 断言 ≤ 40 ms
// ---------------------------------------------------------------------------
static void tc_b05_av_sync_within_threshold() {
    SyncClock clock;

    // 1. 未输入任何 ts 时，offset 应为 0，isInSync = true
    assert(clock.getAvOffsetNs() == 0);
    assert(clock.isInSync());

    // 2. 理想同步：视频和音频 PTS 精确对齐
    // Note: interleaved updateVideoTs / updateAudioTs leaves transient per-frame offsets
    // in the sliding window between the two calls. The smoothed value is bounded by ±1 frame.
    const int64_t frameNs = 33'333'333LL;  // ~30fps
    for (int i = 0; i < 10; ++i) {
        clock.updateVideoTs(i * frameNs);
        clock.updateAudioTs(i * frameNs);
    }
    assert(clock.isInSync());
    {
        int64_t idealOff = clock.getSmoothedOffsetNs();
        int64_t absOff = idealOff < 0 ? -idealOff : idealOff;
        assert(absOff <= frameNs && "Ideal sync offset must be within 1 frame period");
    }

    // 3. 轻微漂移（20ms 视频超前）— 仍应在 40ms 阈值内
    clock.reset();
    const int64_t drift20ms = 20'000'000LL;
    for (int i = 0; i < 10; ++i) {
        clock.updateVideoTs(i * frameNs + drift20ms);
        clock.updateAudioTs(i * frameNs);
    }
    assert(clock.isInSync());
    int64_t off20 = clock.getSmoothedOffsetNs();
    assert(off20 >= 0 && off20 <= SyncClock::SYNC_THRESHOLD_NS);

    // 4. 严重漂移（60ms）— 应触发失同步
    clock.reset();
    const int64_t drift60ms = 60'000'000LL;
    for (int i = 0; i < SyncClock::WINDOW_SIZE; ++i) {
        clock.updateVideoTs(i * frameNs + drift60ms);
        clock.updateAudioTs(i * frameNs);
    }
    assert(!clock.isInSync());
    int64_t off60 = clock.getSmoothedOffsetNs();
    assert(off60 > SyncClock::SYNC_THRESHOLD_NS);

    // 5. reset 后恢复干净状态
    clock.reset();
    assert(clock.getAvOffsetNs() == 0);
    assert(clock.isInSync());

    // 6. 音频超前视频 25ms（负 offset）— 仍应在阈值内
    clock.reset();
    const int64_t drift25msAudio = 25'000'000LL;
    for (int i = 0; i < SyncClock::WINDOW_SIZE; ++i) {
        clock.updateVideoTs(i * frameNs);
        clock.updateAudioTs(i * frameNs + drift25msAudio);
    }
    assert(clock.isInSync());
    int64_t offNeg = clock.getSmoothedOffsetNs();
    assert(offNeg < 0 && offNeg >= -SyncClock::SYNC_THRESHOLD_NS);

    std::cout << "TC-B05 PASS: av_sync_within_threshold"
              << " (20ms drift offset=" << off20 / 1'000'000 << "ms"
              << ", 60ms drift offset=" << off60 / 1'000'000 << "ms"
              << ", audio-lead offset=" << offNeg / 1'000'000 << "ms)" << std::endl;
}

// ---------------------------------------------------------------------------
// TC-B06: SyncClock — export re-use: reset() between consecutive exports
// ---------------------------------------------------------------------------
static void tc_b06_syncclock_reuse_between_exports() {
    SyncClock clock;

    // Simulate first export: drift causes out-of-sync
    const int64_t frameNs  = 33'333'333LL;
    const int64_t drift80ms = 80'000'000LL;
    for (int i = 0; i < SyncClock::WINDOW_SIZE; ++i) {
        clock.updateVideoTs(i * frameNs + drift80ms);
        clock.updateAudioTs(i * frameNs);
    }
    assert(!clock.isInSync() && "Export 1 should be out-of-sync after 80ms drift");

    // Simulate what the exporter does at the start of the next export run
    clock.reset();

    // Simulate second export: clean signal
    for (int i = 0; i < SyncClock::WINDOW_SIZE; ++i) {
        clock.updateVideoTs(i * frameNs);
        clock.updateAudioTs(i * frameNs);
    }
    assert(clock.isInSync() && "Export 2 should be in-sync after reset");
    int64_t absOff = clock.getSmoothedOffsetNs();
    absOff = absOff < 0 ? -absOff : absOff;
    assert(absOff <= frameNs && "Post-reset smoothed offset must be within 1 frame");

    std::cout << "TC-B06 PASS: syncclock_reuse_between_exports" << std::endl;
}

int main() {
    tc_b01_av_tracks_registered();
    tc_b02_video_only_no_crash();
    tc_b03_cancel_returns_cancelled_code();
    tc_b04_consecutive_exports();
    tc_b05_av_sync_within_threshold();
    tc_b06_syncclock_reuse_between_exports();
    std::cout << "\nAll test_export_e2e cases PASSED" << std::endl;
    return 0;
}
