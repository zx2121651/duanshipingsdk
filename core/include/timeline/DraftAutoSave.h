#pragma once
/**
 * DraftAutoSave.h
 *
 * 草稿自动保存管理器。
 *
 * 功能：
 *   - 定时触发草稿序列化（默认每 30s 保存一次）
 *   - 基于 dirty flag 的变更检测（无变更则跳过 I/O）
 *   - 保存到指定目录（每个草稿一个 .draft 文件）
 *   - 保留最多 N 个历史快照（轮转删除旧文件）
 *   - 崩溃恢复：应用启动时扫描最新草稿文件自动加载
 *
 * 用法：
 *   DraftAutoSave autoSave;
 *   autoSave.setDraftDir("/sdcard/VideoSDK/drafts/project_001/");
 *   autoSave.setInterval(30'000);          // 30 秒
 *   autoSave.setMaxSnapshots(10);
 *   autoSave.attachTimeline(timeline);     // 关联时间线
 *
 *   // 在编辑线程每次修改时调用：
 *   autoSave.markDirty();
 *
 *   // 定时器/后台线程触发：
 *   autoSave.tick();   // 若 dirty 且距上次保存超过 interval，则序列化
 *
 *   // 恢复：
 *   if (autoSave.hasRecoveryDraft()) {
 *       autoSave.loadLatest(timeline);
 *   }
 */

#include "Timeline.h"
#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

namespace sdk {
namespace video {
namespace timeline {

class DraftAutoSave {
public:
    DraftAutoSave();
    ~DraftAutoSave();

    // ── 配置 ──────────────────────────────────────────────────────────────

    /** 草稿文件存储目录（不存在时自动创建）。 */
    void setDraftDir(const std::string& dir) { m_draftDir = dir; }
    const std::string& getDraftDir() const   { return m_draftDir; }

    /** 自动保存间隔（毫秒），默认 30000。 */
    void setInterval(int64_t ms) { m_intervalMs = ms; }

    /** 最多保留的历史快照数（最老的自动删除），默认 10。 */
    void setMaxSnapshots(int n) { m_maxSnapshots = std::max(1, n); }

    /** 关联时间线（弱引用，不影响生命周期）。 */
    void attachTimeline(std::shared_ptr<Timeline> timeline);

    // ── 脏标记 ────────────────────────────────────────────────────────────

    /** 标记草稿已修改（需要下次 tick 保存）。 */
    void markDirty() { m_dirty.store(true); }

    /** 是否有未保存的修改。 */
    bool isDirty() const { return m_dirty.load(); }

    // ── 保存 / 加载 ───────────────────────────────────────────────────────

    /**
     * 检查是否需要自动保存并执行（线程安全）。
     * 应在编辑线程或定时器中调用，<1ms 如无需保存。
     * @return true = 本次执行了保存；false = 无需保存或保存失败
     */
    bool tick();

    /** 立即强制保存（忽略 dirty 标记和时间间隔）。 */
    bool saveNow();

    /**
     * 是否存在可恢复的草稿文件。
     * 用于应用启动时提示用户是否恢复。
     */
    bool hasRecoveryDraft() const;

    /**
     * 加载最新草稿到时间线。
     * @param timeline  目标时间线（在其上调用 TimelineDraft::deserialize）
     * @return true = 加载成功
     */
    bool loadLatest(std::shared_ptr<Timeline> timeline);

    /** 列举所有草稿快照文件路径（按时间降序）。 */
    std::vector<std::string> listSnapshots() const;

    /** 删除所有草稿快照。 */
    void clearAll();

    /** 最近一次保存的错误信息。 */
    const std::string& getLastError() const { return m_lastError; }

    /** 设置保存成功回调（可选，从调用线程同步调用）。 */
    void setOnSaved(std::function<void(const std::string& filePath)> cb) {
        m_onSaved = std::move(cb);
    }

    // ── 后台自动保存线程（可选）──────────────────────────────────────────

    /**
     * 启动后台自动保存线程（无需手动调用 tick()）。
     * 线程每 intervalMs 检查一次 dirty 标记。
     */
    void startBackgroundThread();
    void stopBackgroundThread();

private:
    std::string  m_draftDir;
    int64_t      m_intervalMs    = 30'000;
    int          m_maxSnapshots  = 10;
    std::atomic<bool> m_dirty{false};
    int64_t      m_lastSaveMs    = 0;
    std::string  m_lastError;

    std::weak_ptr<Timeline> m_timeline;

    std::function<void(const std::string&)> m_onSaved;

    std::thread       m_bgThread;
    std::atomic<bool> m_stopBg{false};

    std::string buildSnapshotPath() const;
    void        pruneOldSnapshots() const;
    int64_t     nowMs() const;
};

} // namespace timeline
} // namespace video
} // namespace sdk
