#pragma once
#include "Timeline.h"
#include "Compositor.h"
#include <string>
#include <memory>
#include <functional>
#include <atomic>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 跨平台离线导出器接口
 *
 * 用于在后台线程以非实时（尽可能快）的速度将 Timeline 渲染并编码为本地 MP4 文件。
 */
class TimelineExporter {
public:
    enum class State {
        IDLE,
        STARTING,
        EXPORTING,
        COMPLETED,
        CANCELED,
        FAILED
    };

    virtual ~TimelineExporter() = default;

    // 回调函数：返回导出进度 (0.0 - 1.0)
    using ProgressCallback = std::function<void(float)>;
    // 回调函数：返回导出结果 (Result)
    using CompletionCallback = std::function<void(Result)>;

    // 分片导出回调：每生成一个分片文件触发一次，返回该分片的本地路径和分片索引
    using ChunkCallback = std::function<void(const std::string&, int)>;

    /**
     * @brief 工厂方法：根据平台创建对应的导出器实例
     */
    static std::unique_ptr<TimelineExporter> create();

    /**
     * @brief 配置导出参数
     * @param outputPath 输出的本地绝对路径
     * @param width 视频宽
     * @param height 视频高
     * @param fps 帧率 (例如 30)
     * @param bitrate 码率 (bps)
     */
    virtual Result configure(const std::string& outputPath, int width, int height, int fps, int bitrate) = 0;

    /**
     * @brief 配置分片导出（Pipeline Upload 的核心：边导边传）
     * @param chunkDurationNs 每个分片的预期时长（纳秒），通常为 1~2 秒
     * @param onChunkReady 分片生成后的回调，可直接交由上层上传
     */
    virtual void configureChunking(int64_t chunkDurationNs, ChunkCallback onChunkReady) = 0;

    /**
     * @brief 启动异步导出
     * @param timeline 要导出的时间线
     * @param compositor 已初始化的合成器
     * @param onProgress 进度回调
     * @param onComplete 完成或失败回调
     * @return Result 是否成功启动任务
     */
    virtual Result exportAsync(std::shared_ptr<Timeline> timeline,
                               std::shared_ptr<Compositor> compositor,
                               ProgressCallback onProgress,
                               CompletionCallback onComplete) = 0;

    /**
     * @brief 取消导出
     */
    virtual void cancel() = 0;

    /**
     * @brief 获取当前导出状态
     */
    virtual State getState() const = 0;

    /**
     * @brief 获取当前进度 (0.0 - 1.0)
     */
    virtual float getProgress() const = 0;
};

} // namespace timeline
} // namespace video
} // namespace sdk
