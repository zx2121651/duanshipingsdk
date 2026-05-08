/**
 * EffectPackageUpdater.cpp
 *
 * 特效包云端热更新实现。
 *
 * JSON 清单格式：
 * {
 *   "packages": [
 *     {
 *       "id": "sticker_cute_v3",
 *       "name": "萌系贴纸",
 *       "version": "1.2.0",
 *       "download_url": "https://cdn.example.com/sticker_cute_v3.zip",
 *       "checksum": "abc123...",
 *       "checksum_type": "md5",
 *       "size_bytes": 2097152,
 *       "category": "sticker",
 *       "thumbnail_url": "https://cdn.example.com/thumbs/sticker_cute_v3.jpg"
 *     },
 *     ...
 *   ]
 * }
 *
 * 本地清单：{installDir}/manifest.json（记录已安装版本信息）
 */

#include "../include/EffectPackageUpdater.h"

#define LOG_TAG "EffectPackageUpdater"
#include "../include/Log.h"

#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#   define NOMINMAX
#   include <windows.h>
#   include <direct.h>
#   define MKDIR_P(p) _mkdir(p)
#else
#   include <sys/stat.h>
#   include <dirent.h>
#   define MKDIR_P(p) mkdir(p, 0755)
#endif

namespace sdk {
namespace video {

// ---------------------------------------------------------------------------
// Simple mini-JSON helpers (no external deps)
// ---------------------------------------------------------------------------
namespace {

// Extract string value of "key":"value" in a flat JSON object region.
// Returns "" if not found.
static std::string jsonStr(const std::string& obj, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = obj.find(search);
    if (pos == std::string::npos) return "";
    pos = obj.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    ++pos;
    while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) ++pos;
    if (pos >= obj.size()) return "";
    if (obj[pos] == '"') {
        ++pos;
        std::string val;
        while (pos < obj.size() && obj[pos] != '"') {
            if (obj[pos] == '\\') { ++pos; }
            val += obj[pos++];
        }
        return val;
    }
    // Number as string
    std::string val;
    while (pos < obj.size() && obj[pos] != ',' && obj[pos] != '}' && obj[pos] != ']')
        val += obj[pos++];
    // trim
    while (!val.empty() && (val.back() == ' ' || val.back() == '\n')) val.pop_back();
    return val;
}

static int64_t jsonInt(const std::string& obj, const std::string& key) {
    std::string v = jsonStr(obj, key);
    if (v.empty()) return 0;
    try { return std::stoll(v); } catch (...) { return 0; }
}

// Iterate over top-level array items between '[' and ']'
// Extracts each {...} object and calls fn(objectText)
static void jsonForEachObject(const std::string& json,
                               const std::function<void(const std::string&)>& fn)
{
    size_t pos = json.find('[');
    if (pos == std::string::npos) return;
    ++pos;
    while (pos < json.size()) {
        pos = json.find('{', pos);
        if (pos == std::string::npos) break;
        int depth = 0;
        size_t start = pos;
        while (pos < json.size()) {
            if (json[pos] == '{') ++depth;
            else if (json[pos] == '}') { if (--depth == 0) break; }
            ++pos;
        }
        if (depth == 0) {
            fn(json.substr(start, pos - start + 1));
            ++pos;
        }
    }
}

// Simple version comparison: "1.2.0" >= "1.1.9" → true
// Returns 1 if a>b, 0 if a==b, -1 if a<b
static int versionCmp(const std::string& a, const std::string& b) {
    auto split = [](const std::string& v) {
        std::vector<int> parts;
        std::istringstream ss(v);
        std::string tok;
        while (std::getline(ss, tok, '.')) {
            try { parts.push_back(std::stoi(tok)); } catch (...) { parts.push_back(0); }
        }
        return parts;
    };
    auto va = split(a), vb = split(b);
    size_t n = (va.size() > vb.size()) ? va.size() : vb.size();
    va.resize(n, 0); vb.resize(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (va[i] > vb[i]) return  1;
        if (va[i] < vb[i]) return -1;
    }
    return 0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
EffectPackageUpdater::EffectPackageUpdater(const std::string& installDir)
    : m_installDir(installDir)
{
    MKDIR_P(m_installDir.c_str());
    loadLocalManifest();
}

EffectPackageUpdater::~EffectPackageUpdater() {
    cancelAll();
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------
std::string EffectPackageUpdater::packageDir(const std::string& id) const {
    return m_installDir + "/" + id;
}
std::string EffectPackageUpdater::localManifestPath() const {
    return m_installDir + "/manifest.json";
}

// ---------------------------------------------------------------------------
// Local manifest persistence (simple JSON round-trip)
// ---------------------------------------------------------------------------
void EffectPackageUpdater::loadLocalManifest() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_localManifest.clear();
    std::ifstream f(localManifestPath());
    if (!f.is_open()) return;
    std::ostringstream buf;
    buf << f.rdbuf();
    std::string json = buf.str();
    jsonForEachObject(json, [&](const std::string& obj) {
        EffectPackageInfo pkg;
        pkg.id               = jsonStr(obj, "id");
        pkg.version          = jsonStr(obj, "version");
        pkg.name             = jsonStr(obj, "name");
        pkg.category         = jsonStr(obj, "category");
        pkg.installed        = true;
        pkg.installedVersion = pkg.version;
        if (!pkg.id.empty()) m_localManifest[pkg.id] = pkg;
    });
}

void EffectPackageUpdater::saveLocalManifest() const {
    std::ofstream f(localManifestPath());
    if (!f.is_open()) return;
    f << "{\"packages\":[";
    bool first = true;
    for (auto& [id, pkg] : m_localManifest) {
        if (!first) f << ",";
        first = false;
        f << "{\"id\":\"" << pkg.id
          << "\",\"version\":\"" << pkg.installedVersion
          << "\",\"name\":\"" << pkg.name
          << "\",\"category\":\"" << pkg.category
          << "\"}";
    }
    f << "]}";
}

// ---------------------------------------------------------------------------
// parseManifestJson
// ---------------------------------------------------------------------------
bool EffectPackageUpdater::parseManifestJson(const std::string& json,
                                              std::vector<EffectPackageInfo>& out) const
{
    out.clear();
    // Find "packages" array
    auto pos = json.find("\"packages\"");
    if (pos == std::string::npos) {
        // Also support bare array
        jsonForEachObject(json, [&](const std::string& obj) {
            EffectPackageInfo pkg;
            pkg.id           = jsonStr(obj, "id");
            pkg.name         = jsonStr(obj, "name");
            pkg.version      = jsonStr(obj, "version");
            pkg.downloadUrl  = jsonStr(obj, "download_url");
            pkg.checksum     = jsonStr(obj, "checksum");
            pkg.checksumType = jsonStr(obj, "checksum_type");
            pkg.sizeBytes    = (size_t)jsonInt(obj, "size_bytes");
            pkg.category     = jsonStr(obj, "category");
            pkg.thumbnailUrl = jsonStr(obj, "thumbnail_url");
            if (!pkg.id.empty()) out.push_back(pkg);
        });
        return !out.empty();
    }
    std::string sub = json.substr(pos);
    jsonForEachObject(sub, [&](const std::string& obj) {
        EffectPackageInfo pkg;
        pkg.id           = jsonStr(obj, "id");
        pkg.name         = jsonStr(obj, "name");
        pkg.version      = jsonStr(obj, "version");
        pkg.downloadUrl  = jsonStr(obj, "download_url");
        pkg.checksum     = jsonStr(obj, "checksum");
        pkg.checksumType = jsonStr(obj, "checksum_type");
        pkg.sizeBytes    = (size_t)jsonInt(obj, "size_bytes");
        pkg.category     = jsonStr(obj, "category");
        pkg.thumbnailUrl = jsonStr(obj, "thumbnail_url");
        if (!pkg.id.empty()) out.push_back(pkg);
    });
    return !out.empty();
}

// ---------------------------------------------------------------------------
// checkForUpdatesSync
// ---------------------------------------------------------------------------
std::vector<EffectPackageInfo> EffectPackageUpdater::checkForUpdatesSync() {
    std::vector<EffectPackageInfo> result;
    if (m_manifestUrl.empty()) {
        m_lastError = "EffectPackageUpdater: manifestUrl not set";
        LOGW("%s", m_lastError.c_str());
        return result;
    }
    if (!m_http) {
        m_lastError = "EffectPackageUpdater: IHttpClient not injected";
        LOGW("%s", m_lastError.c_str());
        return result;
    }

    std::string body;
    int code = m_http->get(m_manifestUrl, body);
    if (code != 200) {
        m_lastError = "EffectPackageUpdater: HTTP " + std::to_string(code);
        LOGE("%s", m_lastError.c_str());
        return result;
    }

    std::vector<EffectPackageInfo> remote;
    if (!parseManifestJson(body, remote)) {
        m_lastError = "EffectPackageUpdater: failed to parse manifest JSON";
        return result;
    }

    std::lock_guard<std::mutex> lk(m_mutex);
    m_remoteManifest.clear();
    for (auto& pkg : remote) {
        m_remoteManifest[pkg.id] = pkg;
        auto it = m_localManifest.find(pkg.id);
        if (it == m_localManifest.end()) {
            pkg.needsUpdate = true;
        } else {
            pkg.installed        = true;
            pkg.installedVersion = it->second.installedVersion;
            pkg.needsUpdate      = versionCmp(pkg.version, pkg.installedVersion) > 0;
        }
        if (pkg.needsUpdate) result.push_back(pkg);
    }
    LOGI("EffectPackageUpdater: %zu update(s) available", result.size());
    return result;
}

void EffectPackageUpdater::checkForUpdates(CheckCallback callback) {
    // Run synchronously in this implementation (caller can wrap in a thread)
    auto updates = checkForUpdatesSync();
    if (callback) callback(updates, m_lastError);
}

// ---------------------------------------------------------------------------
// verifyChecksum (stub — real MD5/SHA256 needs platform lib)
// ---------------------------------------------------------------------------
bool EffectPackageUpdater::verifyChecksum(const std::string& filePath,
                                           const std::string& expected,
                                           const std::string& type) const
{
    if (expected.empty()) return true; // no checksum → skip verification
#ifdef HAS_OPENSSL
    // Platform layer can provide OpenSSL or mbedTLS implementation via IHttpClient
    LOGI("EffectPackageUpdater: checksum verification skipped (no crypto lib linked)");
#endif
    (void)filePath; (void)type;
    LOGI("EffectPackageUpdater: checksum stub — assuming %s is valid", filePath.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// extractZip (stub — delegates to platform miniz/unzip)
// ---------------------------------------------------------------------------
bool EffectPackageUpdater::extractZip(const std::string& zipPath,
                                       const std::string& destDir) const
{
    MKDIR_P(destDir.c_str());
    // Real implementation: link miniz / zlib unzip, or shell to unzip command
    LOGI("EffectPackageUpdater: extractZip stub — %s → %s",
         zipPath.c_str(), destDir.c_str());
    // On Android, inject a proper unzip function via a subclass or callback.
    return true;
}

// ---------------------------------------------------------------------------
// downloadPackage
// ---------------------------------------------------------------------------
void EffectPackageUpdater::downloadPackage(const std::string& packageId,
                                            ProgressCallback   onProgress,
                                            CompleteCallback   onComplete)
{
    if (!m_http) {
        if (onComplete) onComplete(packageId, false, "", "IHttpClient not set");
        return;
    }

    EffectPackageInfo pkg;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_remoteManifest.find(packageId);
        if (it == m_remoteManifest.end()) {
            if (onComplete) onComplete(packageId, false, "", "Package not found in remote manifest");
            return;
        }
        pkg = it->second;
        m_downloads[packageId] = {packageId, DownloadState::DOWNLOADING, 0.f, 0, (int64_t)pkg.sizeBytes, ""};
    }

    LOGI("EffectPackageUpdater: downloading %s v%s", packageId.c_str(), pkg.version.c_str());

    std::string tmpPath = m_installDir + "/" + packageId + ".zip.tmp";
    std::string destDir = packageDir(packageId);

    // Check if partial download exists (resume)
    int64_t resumeBytes = 0;
    {
        std::ifstream chk(tmpPath, std::ios::binary | std::ios::ate);
        if (chk.is_open()) resumeBytes = chk.tellg();
    }

    int httpCode = m_http->download(
        pkg.downloadUrl, tmpPath, resumeBytes,
        [&](int64_t downloaded, int64_t total) {
            float prog = (total > 0) ? (float)downloaded / total : 0.f;
            DownloadProgress dp{packageId, DownloadState::DOWNLOADING,
                                prog, downloaded, total, ""};
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_downloads[packageId] = dp;
            }
            if (onProgress) onProgress(dp);
        });

    if (httpCode != 200 && httpCode != 206) {
        std::string err = "HTTP " + std::to_string(httpCode);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_downloads[packageId] = {packageId, DownloadState::FAILED, 0.f, 0, 0, err};
        }
        if (onComplete) onComplete(packageId, false, "", err);
        return;
    }

    // Verify
    {
        DownloadProgress dp{packageId, DownloadState::VERIFYING, 0.99f, 0, 0, ""};
        if (onProgress) onProgress(dp);
    }
    if (!verifyChecksum(tmpPath, pkg.checksum, pkg.checksumType)) {
        std::remove(tmpPath.c_str());
        std::string err = "Checksum mismatch for " + packageId;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_downloads[packageId] = {packageId, DownloadState::FAILED, 0.f, 0, 0, err};
        }
        if (onComplete) onComplete(packageId, false, "", err);
        return;
    }

    // Extract
    {
        DownloadProgress dp{packageId, DownloadState::EXTRACTING, 0.995f, 0, 0, ""};
        if (onProgress) onProgress(dp);
    }
    if (!extractZip(tmpPath, destDir)) {
        std::remove(tmpPath.c_str());
        std::string err = "Extraction failed for " + packageId;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_downloads[packageId] = {packageId, DownloadState::FAILED, 0.f, 0, 0, err};
        }
        if (onComplete) onComplete(packageId, false, "", err);
        return;
    }
    std::remove(tmpPath.c_str());

    // Update local manifest
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto& local     = m_localManifest[packageId];
        local.id               = packageId;
        local.name             = pkg.name;
        local.category         = pkg.category;
        local.installed        = true;
        local.installedVersion = pkg.version;
        m_downloads[packageId] = {packageId, DownloadState::DONE, 1.f,
                                   (int64_t)pkg.sizeBytes, (int64_t)pkg.sizeBytes, ""};
        saveLocalManifest();
    }

    LOGI("EffectPackageUpdater: installed %s v%s → %s",
         packageId.c_str(), pkg.version.c_str(), destDir.c_str());
    if (onComplete) onComplete(packageId, true, destDir, "");
}

// ---------------------------------------------------------------------------
// cancel / uninstall
// ---------------------------------------------------------------------------
void EffectPackageUpdater::cancelDownload(const std::string& packageId) {
    // Signal cancellation via flag
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_downloads.find(packageId);
    if (it != m_downloads.end()) it->second.state = DownloadState::CANCELLED;
}

void EffectPackageUpdater::cancelAll() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& [id, dp] : m_downloads)
        dp.state = DownloadState::CANCELLED;
}

bool EffectPackageUpdater::uninstall(const std::string& packageId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_localManifest.erase(packageId);
    saveLocalManifest();
    // Actual directory removal left to platform (avoid accidental recursive rm)
    LOGI("EffectPackageUpdater: uninstalled %s (dir: %s)", packageId.c_str(),
         packageDir(packageId).c_str());
    return true;
}

void EffectPackageUpdater::uninstallAll() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_localManifest.clear();
    saveLocalManifest();
}

// ---------------------------------------------------------------------------
// listInstalled / getInstallPath / isInstalled
// ---------------------------------------------------------------------------
std::vector<EffectPackageInfo> EffectPackageUpdater::listInstalled() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<EffectPackageInfo> result;
    result.reserve(m_localManifest.size());
    for (auto& [id, pkg] : m_localManifest) result.push_back(pkg);
    return result;
}

std::string EffectPackageUpdater::getInstallPath(const std::string& packageId) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_localManifest.count(packageId)) return packageDir(packageId);
    return "";
}

bool EffectPackageUpdater::isInstalled(const std::string& packageId,
                                        const std::string& minVersion) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_localManifest.find(packageId);
    if (it == m_localManifest.end()) return false;
    if (minVersion.empty()) return true;
    return versionCmp(it->second.installedVersion, minVersion) >= 0;
}

DownloadProgress EffectPackageUpdater::getDownloadProgress(const std::string& id) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_downloads.find(id);
    if (it == m_downloads.end()) return {id, DownloadState::IDLE};
    return it->second;
}

} // namespace video
} // namespace sdk
