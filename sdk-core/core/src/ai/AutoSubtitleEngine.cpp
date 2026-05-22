/**
 * AutoSubtitleEngine.cpp
 *
 * 工厂 + 异步包装实现。
 * 平台实现（VoskEngine / WhisperEngine）通过 registerFactory() 注入。
 */

#include "../../include/ai/AutoSubtitleEngine.h"

#define LOG_TAG "AutoSubtitleEngine"
#include "../../include/Log.h"

#include <thread>
#include <mutex>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// 全局工厂注册
// ---------------------------------------------------------------------------
static std::mutex s_factoryMutex;
static std::function<std::shared_ptr<AutoSubtitleEngine>()> s_factory;

void AutoSubtitleEngine::registerFactory(
    std::function<std::shared_ptr<AutoSubtitleEngine>()> factory)
{
    std::lock_guard<std::mutex> lk(s_factoryMutex);
    s_factory = std::move(factory);
    LOGI("AutoSubtitleEngine: factory registered");
}

std::shared_ptr<AutoSubtitleEngine> AutoSubtitleEngine::createDefault() {
    std::lock_guard<std::mutex> lk(s_factoryMutex);
    if (s_factory) {
        auto engine = s_factory();
        if (engine) return engine;
    }
    LOGW("AutoSubtitleEngine: no factory registered — returning stub");
    return std::make_shared<StubAutoSubtitleEngine>();
}

// ---------------------------------------------------------------------------
// recognizeAsync() — 通用异步包装
// ---------------------------------------------------------------------------
void AutoSubtitleEngine::recognizeAsync(
    const std::string& audioPath,
    int64_t durationNs,
    std::function<void(std::vector<SubtitleSegment>, std::string)> onDone)
{
    // Capture this + args by value into a detached thread.
    // Production code should use a thread pool; detach is acceptable here
    // because the callback notifies completion.
    auto self = shared_from_this();   // requires enable_shared_from_this — see below
    std::thread([self, audioPath, durationNs, onDone]() {
        try {
            auto segs = self->recognize(audioPath, durationNs);
            if (onDone) onDone(std::move(segs), "");
        } catch (const std::exception& e) {
            if (onDone) onDone({}, std::string(e.what()));
        }
    }).detach();
}

} // namespace ai
} // namespace video
} // namespace sdk
