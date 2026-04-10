#pragma once
#include "Timeline.h"
#include "Compositor.h"
#include <string>
#include <memory>
#include <functional>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 跨平台离线导出器接口
 *
 * 用于在后台线程以非实时（尽可能快）的速度将 Timeline 渲染并编码为本地 MP4 文件。
 */
/**
 * @Experimental
 * Note: Offline exporting is currently in an experimental phase and APIs may change.
 */
class TimelineExporter {
public:
    virtual ~TimelineExporter() = default;

    // 回调函数：返回导出进度 (0.0 - 1.0)
    using ProgressCallback = std::function<void(float)>;
    // 回调函数：返回导出结果 (ErrorCode)
    using CompletionCallback = std::function<void(Result)>;

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
     * @brief 启动异步导出
     * @param timeline 要导出的时间线
     * @param compositor 已初始化的合成器
     * @param onProgress 进度回调
     * @param onComplete 完成或失败回调
     */
    virtual void exportAsync(std::shared_ptr<Timeline> timeline,
                             std::shared_ptr<Compositor> compositor,
                             ProgressCallback onProgress,
                             CompletionCallback onComplete) = 0;

    /**
     * @brief 取消导出
     */
    virtual void cancel() = 0;
};

} // namespace timeline
} // namespace video
} // namespace sdk
