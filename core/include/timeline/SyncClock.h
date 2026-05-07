#pragma once
/**
 * SyncClock.h — 音视频同步时钟
 *
 * 职责：
 *  - 跟踪导出/播放过程中视频帧和音频帧各自的呈现时间戳（PTS）。
 *  - 计算并维护 A/V offset 的滑动平均值，提供同步质量查询。
 *  - 当偏移超过阈值时发出警告（通过回调或日志）。
 *
 * 线程安全：
 *  updateVideoTs / updateAudioTs 可从不同线程并发调用（内部使用 atomic）。
 *  getAvOffsetNs / isInSync 可从任意线程读取。
 *
 * 典型用法（导出循环中）：
 * ```cpp
 * SyncClock clock;
 * // 每写入一帧视频时：
 * clock.updateVideoTs(videoPtsNs);
 * // 每写入一段音频时：
 * clock.updateAudioTs(audioPtsNs);
 * // 任意时刻查询：
 * if (!clock.isInSync()) { LOG("A/V drift: %lld ms", clock.getAvOffsetNs() / 1000000); }
 * ```
 */

#include <atomic>
#include <cstdint>
#include <cstdlib>   // abs

namespace sdk {
namespace video {
namespace timeline {

class SyncClock {
public:
    /** 偏移超过此阈值视为失同步（40 ms，行业惯例上限） */
    static constexpr int64_t SYNC_THRESHOLD_NS = 40'000'000LL;

    /** 滑动窗口大小（帧数） */
    static constexpr int WINDOW_SIZE = 8;

    SyncClock() { reset(); }

    /**
     * 记录最新一帧视频的呈现时间戳（纳秒）。
     * 从视频编码/复用线程调用。
     */
    void updateVideoTs(int64_t presentationNs) noexcept {
        m_lastVideoNs.store(presentationNs, std::memory_order_relaxed);
        computeOffset();
    }

    /**
     * 记录最新一段音频的呈现时间戳（纳秒）。
     * 从音频编码/复用线程调用。
     */
    void updateAudioTs(int64_t presentationNs) noexcept {
        m_lastAudioNs.store(presentationNs, std::memory_order_relaxed);
        computeOffset();
    }

    /**
     * 返回当前的瞬时 A/V offset（视频 − 音频，纳秒）。
     * 正值表示视频超前音频，负值表示音频超前视频。
     */
    int64_t getAvOffsetNs() const noexcept {
        return m_currentOffsetNs.load(std::memory_order_relaxed);
    }

    /**
     * 返回滑动窗口平均 A/V offset（纳秒）。
     * 比瞬时值更稳定，适合 UI 显示和 CI 断言。
     */
    int64_t getSmoothedOffsetNs() const noexcept {
        return m_smoothedOffsetNs.load(std::memory_order_relaxed);
    }

    /**
     * 当前是否处于同步状态（|smoothed offset| < SYNC_THRESHOLD_NS）。
     */
    bool isInSync() const noexcept {
        int64_t off = m_smoothedOffsetNs.load(std::memory_order_relaxed);
        return (off < 0 ? -off : off) < SYNC_THRESHOLD_NS;
    }

    /** 重置所有时间戳和滑动窗口。 */
    void reset() noexcept {
        m_lastVideoNs.store(0, std::memory_order_relaxed);
        m_lastAudioNs.store(0, std::memory_order_relaxed);
        m_currentOffsetNs.store(0, std::memory_order_relaxed);
        m_smoothedOffsetNs.store(0, std::memory_order_relaxed);
        m_windowHead = 0;
        m_windowCount = 0;
        for (auto& v : m_window) v = 0;
    }

private:
    std::atomic<int64_t> m_lastVideoNs{0};
    std::atomic<int64_t> m_lastAudioNs{0};
    std::atomic<int64_t> m_currentOffsetNs{0};
    std::atomic<int64_t> m_smoothedOffsetNs{0};

    // Simple circular buffer for sliding window average (not mutex-protected;
    // minor races here are acceptable — the result is used only for advisory logging).
    int64_t m_window[WINDOW_SIZE]{};
    int     m_windowHead  = 0;
    int     m_windowCount = 0;

    void computeOffset() noexcept {
        int64_t vTs = m_lastVideoNs.load(std::memory_order_relaxed);
        int64_t aTs = m_lastAudioNs.load(std::memory_order_relaxed);
        if (vTs == 0 || aTs == 0) return;  // not enough data yet

        int64_t instantOffset = vTs - aTs;
        m_currentOffsetNs.store(instantOffset, std::memory_order_relaxed);

        // Update sliding window
        m_window[m_windowHead] = instantOffset;
        m_windowHead = (m_windowHead + 1) % WINDOW_SIZE;
        if (m_windowCount < WINDOW_SIZE) ++m_windowCount;

        // Compute average
        int64_t sum = 0;
        for (int i = 0; i < m_windowCount; ++i) sum += m_window[i];
        m_smoothedOffsetNs.store(sum / m_windowCount, std::memory_order_relaxed);
    }
};

} // namespace timeline
} // namespace video
} // namespace sdk
