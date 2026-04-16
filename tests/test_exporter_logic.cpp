#include <iostream>
#include <memory>
#include <cassert>
#include <thread>
#include <chrono>
#include "../core/include/timeline/TimelineExporter.h"
#include "../core/include/timeline/Timeline.h"
#include "../core/include/FilterEngine.h"

using namespace sdk::video;
using namespace sdk::video::timeline;

// Mock Compositor for logic testing
class MockTimelineExporter : public TimelineExporter {
public:
    Result configure(const std::string& outputPath, int width, int height, int fps, int bitrate) override {
        m_state = State::IDLE;
        return Result::ok();
    }
    void configureChunking(int64_t chunkDurationNs, ChunkCallback onChunkReady) override {}
    Result exportAsync(std::shared_ptr<Timeline> timeline,
                               std::shared_ptr<Compositor> compositor,
                               ProgressCallback onProgress,
                               CompletionCallback onComplete) override {
        m_state = State::EXPORTING;
        m_thread = std::thread([this, onProgress, onComplete]() {
            for (int i = 1; i <= 10; ++i) {
                if (m_state == State::CANCELED) break;
                m_progress = i / 10.0f;
                if (onProgress) onProgress(m_progress);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (m_state != State::CANCELED) {
                m_state = State::COMPLETED;
                if (onComplete) onComplete(Result::ok());
            } else {
                if (onComplete) onComplete(Result::error(ErrorCode::ERR_EXPORTER_CANCELLED, "Cancelled"));
            }
        });
        return Result::ok();
    }
    void cancel() override {
        m_state = State::CANCELED;
    }
    State getState() const override { return m_state; }
    float getProgress() const override { return m_progress; }

    void wait() {
        if (m_thread.joinable()) m_thread.join();
    }

private:
    std::atomic<State> m_state{State::IDLE};
    std::atomic<float> m_progress{0.0f};
    std::thread m_thread;
};

void test_basic_export_flow() {
    auto exporter = std::make_unique<MockTimelineExporter>();
    assert(exporter->getState() == TimelineExporter::State::IDLE);

    exporter->configure("test.mp4", 1280, 720, 30, 4000000);

    bool completed = false;
    exporter->exportAsync(nullptr, nullptr,
        [](float p) { std::cout << "Progress: " << p << std::endl; },
        [&completed](Result res) {
            assert(res.isOk());
            completed = true;
        }
    );

    assert(exporter->getState() == TimelineExporter::State::EXPORTING);
    exporter->wait();
    assert(exporter->getState() == TimelineExporter::State::COMPLETED);
    assert(completed);
    std::cout << "test_basic_export_flow passed" << std::endl;
}

void test_cancel_export() {
    auto exporter = std::make_unique<MockTimelineExporter>();
    exporter->configure("test.mp4", 1280, 720, 30, 4000000);

    bool completedCalled = false;
    exporter->exportAsync(nullptr, nullptr, nullptr,
        [&completedCalled](Result res) {
            assert(!res.isOk());
            assert(res.getErrorCode() == ErrorCode::ERR_EXPORTER_CANCELLED);
            completedCalled = true;
        }
    );

    exporter->cancel();
    exporter->wait();
    assert(exporter->getState() == TimelineExporter::State::CANCELED);
    assert(completedCalled);
    std::cout << "test_cancel_export passed" << std::endl;
}

int main() {
    test_basic_export_flow();
    test_cancel_export();
    return 0;
}
