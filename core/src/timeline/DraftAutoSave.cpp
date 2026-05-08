/**
 * DraftAutoSave.cpp
 *
 * 草稿自动保存实现。
 * 序列化使用现有 TimelineDraft。
 */

#include "../../include/timeline/DraftAutoSave.h"
#include "../../include/timeline/TimelineDraft.h"
#include "../../include/timeline/SubtitleClip.h"

#define LOG_TAG "DraftAutoSave"
#include "../../include/Log.h"

#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifdef _WIN32
#   include <windows.h>
#   include <io.h>
#   include <direct.h>
#   define MKDIR(p) _mkdir(p)
#else
#   include <dirent.h>
#   include <sys/stat.h>
#   define MKDIR(p) mkdir(p, 0755)
#endif

namespace sdk {
namespace video {
namespace timeline {

// ---------------------------------------------------------------------------
// Helper: current time ms
// ---------------------------------------------------------------------------
int64_t DraftAutoSave::nowMs() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
DraftAutoSave::DraftAutoSave() = default;

DraftAutoSave::~DraftAutoSave() {
    stopBackgroundThread();
}

void DraftAutoSave::attachTimeline(std::shared_ptr<Timeline> timeline) {
    m_timeline = timeline;
}

// ---------------------------------------------------------------------------
// buildSnapshotPath — e.g. drafts/2026-05-08_18-30-45_123.draft
// ---------------------------------------------------------------------------
std::string DraftAutoSave::buildSnapshotPath() const {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t   = system_clock::to_time_t(now);
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    std::ostringstream oss;
    oss << m_draftDir;
#ifdef _WIN32
    if (m_draftDir.back() != '\\' && m_draftDir.back() != '/') oss << '\\';
#else
    if (m_draftDir.back() != '/') oss << '/';
#endif
    oss << std::put_time(&tm_buf, "%Y-%m-%d_%H-%M-%S")
        << '_' << std::setw(3) << std::setfill('0') << ms
        << ".draft";
    return oss.str();
}

// ---------------------------------------------------------------------------
// pruneOldSnapshots
// ---------------------------------------------------------------------------
void DraftAutoSave::pruneOldSnapshots() const {
    auto snaps = const_cast<DraftAutoSave*>(this)->listSnapshots();
    while ((int)snaps.size() > m_maxSnapshots) {
        // listSnapshots is sorted newest-first; delete the oldest = last
        std::remove(snaps.back().c_str());
        snaps.pop_back();
    }
}

// ---------------------------------------------------------------------------
// listSnapshots — sorted newest-first
// ---------------------------------------------------------------------------
std::vector<std::string> DraftAutoSave::listSnapshots() const {
    std::vector<std::string> result;
    if (m_draftDir.empty()) return result;

#ifdef _WIN32
    std::string pattern = m_draftDir + "\\*.draft";
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return result;
    do {
        result.push_back(m_draftDir + "\\" + ffd.cFileName);
    } while (FindNextFileA(h, &ffd));
    FindClose(h);
#else
    DIR* dir = opendir(m_draftDir.c_str());
    if (!dir) return result;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string n = ent->d_name;
        if (n.size() >= 6 && n.substr(n.size() - 6) == ".draft")
            result.push_back(m_draftDir + "/" + n);
    }
    closedir(dir);
#endif

    std::sort(result.begin(), result.end(), std::greater<std::string>());
    return result;
}

// ---------------------------------------------------------------------------
// saveNow
// ---------------------------------------------------------------------------
bool DraftAutoSave::saveNow() {
    auto tl = m_timeline.lock();
    if (!tl) {
        m_lastError = "DraftAutoSave: no timeline attached";
        LOGW("%s", m_lastError.c_str());
        return false;
    }

    // Ensure directory exists
    if (!m_draftDir.empty()) {
        MKDIR(m_draftDir.c_str());
    }

    std::string path = buildSnapshotPath();
    bool ok = saveDraft(*tl, path);
    if (!ok) {
        m_lastError = "DraftAutoSave: saveDraft failed for " + path;
        LOGE("%s", m_lastError.c_str());
        return false;
    }

    m_dirty.store(false);
    m_lastSaveMs = nowMs();
    pruneOldSnapshots();

    LOGI("DraftAutoSave: saved %s", path.c_str());
    if (m_onSaved) m_onSaved(path);
    return true;
}

// ---------------------------------------------------------------------------
// tick
// ---------------------------------------------------------------------------
bool DraftAutoSave::tick() {
    if (!m_dirty.load()) return false;
    int64_t elapsed = nowMs() - m_lastSaveMs;
    if (elapsed < m_intervalMs) return false;
    return saveNow();
}

// ---------------------------------------------------------------------------
// hasRecoveryDraft / loadLatest
// ---------------------------------------------------------------------------
bool DraftAutoSave::hasRecoveryDraft() const {
    return !listSnapshots().empty();
}

bool DraftAutoSave::loadLatest(std::shared_ptr<Timeline> /*timeline*/) {
    auto snaps = listSnapshots();
    if (snaps.empty()) {
        m_lastError = "DraftAutoSave: no snapshots to load";
        return false;
    }

    auto rebuilt = loadDraft(snaps.front());
    bool ok = (rebuilt != nullptr);
    if (ok) {
        m_timeline = rebuilt;
    }
    if (!ok) {
        m_lastError = "DraftAutoSave: deserialize failed for " + snaps.front();
        LOGE("%s", m_lastError.c_str());
    } else {
        LOGI("DraftAutoSave: loaded %s", snaps.front().c_str());
        m_dirty.store(false);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// clearAll
// ---------------------------------------------------------------------------
void DraftAutoSave::clearAll() {
    for (auto& p : listSnapshots()) std::remove(p.c_str());
}

// ---------------------------------------------------------------------------
// Background thread
// ---------------------------------------------------------------------------
void DraftAutoSave::startBackgroundThread() {
    m_stopBg.store(false);
    m_bgThread = std::thread([this]() {
        while (!m_stopBg.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            tick();
        }
    });
}

void DraftAutoSave::stopBackgroundThread() {
    m_stopBg.store(true);
    if (m_bgThread.joinable()) m_bgThread.join();
}

} // namespace timeline
} // namespace video
} // namespace sdk
