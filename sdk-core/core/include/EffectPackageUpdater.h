#pragma once
/**
 * EffectPackageUpdater.h
 *
 * 特效包云端热更新管理器。
 *
 * 功能：
 *   - 检查远端清单（manifest URL）获取最新版本信息
 *   - 对比本地已安装版本，决定是否需要下载
 *   - 分块下载 .zip 特效包，支持断点续传
 *   - 校验 MD5/SHA256、解压到指定目录
 *   - 回调通知进度/完成/失败
 *   - 支持 A/B 分包切换（下载完成后原子切换，不影响当前播放）
 *
 * 与 ModelAssetManager（AI模型OTA）互补：
 *   本模块负责 shader + 资源包（贴纸动画、LUT、模板等）热更新。
 *
 * 使用流程：
 *   EffectPackageUpdater updater("/sdcard/effects/");
 *   updater.setManifestUrl("https://cdn.example.com/effects/manifest.json");
 *   updater.checkForUpdates([](std::vector<EffectPackageInfo> updates) {
 *       for (auto& pkg : updates) {
 *           updater.downloadPackage(pkg.id, onProgress, onComplete);
 *       }
 *   });
 */

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <map>
#include <mutex>
#include <atomic>

namespace sdk {
namespace video {

// ---------------------------------------------------------------------------
// 特效包元信息
// ---------------------------------------------------------------------------
struct EffectPackageInfo {
    std::string id;           ///< 唯一标识，如 "sticker_cute_v3"
    std::string name;         ///< 显示名称
    std::string version;      ///< 语义版本号，如 "1.2.0"
    std::string downloadUrl;  ///< ZIP 下载地址
    std::string checksum;     ///< MD5 或 SHA256 (hex)
    std::string checksumType; ///< "md5" | "sha256"
    size_t      sizeBytes = 0;///< ZIP 包大小（字节）
    std::string category;     ///< "sticker" | "lut" | "template" | "shader"
    std::string thumbnailUrl; ///< 预览图 URL（可选）
    bool        installed   = false; ///< 本地已安装
    std::string installedVersion;    ///< 本地已安装版本
    bool        needsUpdate = false; ///< 有更新可用
};

// ---------------------------------------------------------------------------
// 下载状态
// ---------------------------------------------------------------------------
enum class DownloadState {
    IDLE,
    CHECKING,
    DOWNLOADING,
    VERIFYING,
    EXTRACTING,
    DONE,
    FAILED,
    CANCELLED,
};

struct DownloadProgress {
    std::string packageId;
    DownloadState state     = DownloadState::IDLE;
    float   progress        = 0.f;  ///< [0,1]
    int64_t bytesDownloaded = 0;
    int64_t bytesTotal      = 0;
    std::string errorMessage;
};

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------
using CheckCallback    = std::function<void(const std::vector<EffectPackageInfo>& updates,
                                             const std::string& error)>;
using ProgressCallback = std::function<void(const DownloadProgress&)>;
using CompleteCallback = std::function<void(const std::string& packageId,
                                             bool success,
                                             const std::string& installPath,
                                             const std::string& error)>;

// ---------------------------------------------------------------------------
// IHttpClient — 平台层注入（Android 使用 OkHttp / libcurl）
// ---------------------------------------------------------------------------
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    /**
     * 同步 GET 请求，返回响应体字符串。
     * @param url   请求 URL
     * @param body  输出响应体
     * @return HTTP 状态码（200=成功）
     */
    virtual int get(const std::string& url, std::string& body) = 0;

    /**
     * 流式下载文件（支持断点续传）。
     * @param url         下载 URL
     * @param destPath    本地临时文件路径
     * @param resumeBytes 已下载字节数（0=从头开始）
     * @param onProgress  进度回调 (downloaded, total)
     * @return HTTP 状态码
     */
    virtual int download(const std::string& url,
                         const std::string& destPath,
                         int64_t            resumeBytes,
                         std::function<void(int64_t, int64_t)> onProgress) = 0;
};

// ---------------------------------------------------------------------------
// EffectPackageUpdater
// ---------------------------------------------------------------------------
class EffectPackageUpdater {
public:
    explicit EffectPackageUpdater(const std::string& installDir);
    ~EffectPackageUpdater();

    // ── 配置 ──────────────────────────────────────────────────────────────

    /** 远端清单 URL，JSON 格式，包含 EffectPackageInfo 数组。 */
    void setManifestUrl(const std::string& url) { m_manifestUrl = url; }

    /** 注入 HTTP 客户端（Android 传 OkHttp 包装，桌面传 libcurl 包装）。 */
    void setHttpClient(std::shared_ptr<IHttpClient> client) { m_http = client; }

    /** 最大并发下载数，默认 2。 */
    void setMaxConcurrentDownloads(int n) { m_maxConcurrent = n; }

    /** 每次下载块大小（字节），用于进度回调粒度，默认 64KB。 */
    void setChunkSize(size_t bytes) { m_chunkSize = bytes; }

    // ── 查询本地安装 ──────────────────────────────────────────────────────

    /** 列出所有已安装特效包。 */
    std::vector<EffectPackageInfo> listInstalled() const;

    /** 获取指定包安装路径，未安装返回空字符串。 */
    std::string getInstallPath(const std::string& packageId) const;

    /** 是否已安装指定版本以上的包。 */
    bool isInstalled(const std::string& packageId,
                     const std::string& minVersion = "") const;

    // ── 远端检查 ──────────────────────────────────────────────────────────

    /**
     * 异步检查远端是否有更新。
     * @param callback  检查完成后调用（在调用线程返回前可能是同步的，
     *                  也可能由 HTTP 线程回调——由注入的 IHttpClient 决定）
     */
    void checkForUpdates(CheckCallback callback);

    /** 同步检查（阻塞调用线程，测试用）。 */
    std::vector<EffectPackageInfo> checkForUpdatesSync();

    // ── 下载 / 安装 ───────────────────────────────────────────────────────

    /**
     * 下载并安装指定特效包（异步）。
     * @param packageId   包 ID（来自 checkForUpdates 返回列表）
     * @param onProgress  进度回调
     * @param onComplete  完成/失败回调
     */
    void downloadPackage(const std::string& packageId,
                         ProgressCallback   onProgress,
                         CompleteCallback   onComplete);

    /** 取消正在进行的下载。 */
    void cancelDownload(const std::string& packageId);

    /** 取消所有下载。 */
    void cancelAll();

    // ── 卸载 ──────────────────────────────────────────────────────────────

    /** 卸载（删除）指定特效包。 */
    bool uninstall(const std::string& packageId);

    /** 清空所有已安装包。 */
    void uninstallAll();

    // ── 状态查询 ──────────────────────────────────────────────────────────

    DownloadProgress getDownloadProgress(const std::string& packageId) const;

    const std::string& getLastError() const { return m_lastError; }

private:
    std::string m_installDir;
    std::string m_manifestUrl;
    std::shared_ptr<IHttpClient> m_http;
    int    m_maxConcurrent = 2;
    size_t m_chunkSize     = 64 * 1024;
    mutable std::string m_lastError;

    // Local manifest cache: packageId → EffectPackageInfo
    mutable std::mutex m_mutex;
    std::map<std::string, EffectPackageInfo> m_remoteManifest;
    std::map<std::string, EffectPackageInfo> m_localManifest;
    std::map<std::string, DownloadProgress>  m_downloads;
    std::map<std::string, std::atomic<bool>> m_cancelFlags;

    void loadLocalManifest();
    void saveLocalManifest() const;
    bool verifyChecksum(const std::string& filePath,
                        const std::string& expected,
                        const std::string& type) const;
    bool extractZip(const std::string& zipPath,
                    const std::string& destDir) const;
    bool parseManifestJson(const std::string& json,
                           std::vector<EffectPackageInfo>& out) const;

    std::string packageDir(const std::string& packageId) const;
    std::string localManifestPath() const;
};

} // namespace video
} // namespace sdk
