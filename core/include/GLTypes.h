#pragma once
#include <cstdint>

#include <string>
#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES2/glext.h>
    #include <OpenGLES/ES3/glext.h>
#else
    #include <GLES3/gl3.h>
    // 引入 OpenGL ES 3.1 的扩展，以支持 Compute Shader 和 Image Store
    #include <GLES3/gl31.h>
#endif

namespace sdk {
namespace video {

namespace rhi {
    class ITexture;
}

struct Texture {
    uint32_t id;
    uint32_t width;
    uint32_t height;
};


// ----------------------------------------------------------------------------
// SDK 统一错误码字典 (Unified Error Code Dictionary)
//
// 错误码分类原则：
// 1. FATAL (致命错误): -1000 到 -1999。通常指初始化失败，引擎无法启动，需重启 App 或重建 Engine。
// 2. RECOVERABLE/DEGRADED (可恢复/降级错误): -2000 到 -4999。指运行期异常，可通过降级逻辑（如绕过滤镜、切换软解）继续运行。
// 3. EXPORTER (导出错误): -5000 到 -5999。专门针对离线导出任务。
// ----------------------------------------------------------------------------
enum ErrorCode {
    SUCCESS = 0,

    // --- [Category: Initialization] (Fatal) Range: -1000 ~ -1999 ---
    // 这些错误通常意味着基础环境不满足要求
    ERR_INIT_CONTEXT_FAILED = -1001,    // EGL/EAGL 上下文创建失败
    ERR_INIT_SHADER_FAILED = -1002,     // 基础系统 Shader 编译失败
    ERR_INIT_OBOE_FAILED = -1003,       // Android Oboe 音频引擎初始化失败

    // --- [Category: Rendering] (Recoverable) Range: -2000 ~ -2999 ---
    // 渲染管线内部错误，建议进入 DEGRADED 模式（跳过滤镜）
    ERR_RENDER_FBO_ALLOC_FAILED = -2001,       // 显存不足，无法分配帧缓冲
    ERR_RENDER_INVALID_STATE = -2002,          // 状态异常（如在非渲染线程调用，或管线未编译）
    ERR_RENDER_COMPUTE_NOT_SUPPORTED = -2003,  // 当前硬件不支持 Compute Shader

    // --- [Category: Timeline & Decoder] (Recoverable) Range: -3000 ~ -3999 ---
    // 编辑、解码相关错误，建议触发重试或软解回退
    ERR_TIMELINE_NULL = -3001,               // Timeline 实例为空
    ERR_TIMELINE_TRACK_NOT_FOUND = -3002,    // 找不到指定的轨道
    ERR_TIMELINE_CLIP_NOT_FOUND = -3003,     // 找不到指定的剪辑
    ERR_TIMELINE_DECODER_POOL_NULL = -3004,  // 解码池未初始化
    ERR_TIMELINE_DECODER_GET_FRAME_FAILED = -3005, // 解码器获取帧失败
    ERR_TIMELINE_COMPOSITOR_INIT_FAILED = -3006,   // 渲染合成器初始化失败
    ERR_DECODER_SEEK_FAILED = -3007,         // 定位失败（通常触发硬解转软解）
    ERR_DECODER_FRAME_DROP = -3008,          // 解码掉帧
    ERR_DECODER_HW_FAILURE = -3009,          // 硬件解码器崩溃

    // --- [Category: Graph Compilation] (Recoverable) Range: -4000 ~ -4999 ---
    // 滤镜图构建错误，建议回滚到上一个有效图状态
    ERR_GRAPH_CYCLE_DETECTED = -4001,   // 渲染图中存在环路
    ERR_GRAPH_NO_SINK = -4002,          // 图中没有输出节点
    ERR_GRAPH_NODE_INIT_FAILED = -4003, // 节点内部初始化（如私有 Shader）失败

    // --- [Category: Exporter] Range: -5000 ~ -5999 ---
    // 专门用于离线导出任务的错误
    ERR_EXPORTER_ALREADY_RUNNING = -5001,   // 导出任务已在运行
    ERR_EXPORTER_NOT_CONFIGURED = -5002,    // 导出参数未配置
    ERR_EXPORTER_ENCODER_INIT_FAILED = -5003, // 编码器初始化失败
    ERR_EXPORTER_MUXER_INIT_FAILED = -5004,   // 封装器初始化失败
    ERR_EXPORTER_GL_CONTEXT_FAILED = -5005,   // 导出专用 GL 上下文创建失败
    ERR_EXPORTER_CANCELLED = -5006,           // 用户主动取消导出
    ERR_EXPORTER_IO_ERROR = -5007             // 文件读写 I/O 错误
};


// ----------------------------------------------------------------------------
// 导出/录制 配置参数
// ----------------------------------------------------------------------------
struct VideoExportConfig {
    int width = 1080;
    int height = 1920;
    int fps = 30;
    int videoBitrate = 10000000; // 10 Mbps
    int audioBitrate = 128000;   // 128 Kbps
    int gopSize = 30;            // I-frame interval (frames)
    bool useHwEncoder = true;
    bool enableHdr = false;
};
class Result {
public:
    static Result ok() { return Result(true, ErrorCode::SUCCESS, ""); }
    static Result error(int code, const std::string& msg) { return Result(false, code, msg); }
    static Result error(const std::string& msg) { return Result(false, -1, msg); }

    bool isOk() const { return m_isOk; }
    int getErrorCode() const { return m_errorCode; }
    std::string getMessage() const { return m_message; }

protected:
    Result(bool isOk, int code, const std::string& msg) : m_isOk(isOk), m_errorCode(code), m_message(msg) {}
    bool m_isOk;
    int m_errorCode;
    std::string m_message;
};

// 带有负载数据的 Result<T> 泛型模板，用于替换直接返回值的危险做法
template <typename T>
class ResultPayload : public Result {
public:
    static ResultPayload<T> ok(const T& val) { return ResultPayload<T>(true, ErrorCode::SUCCESS, "", val); }
    static ResultPayload<T> error(int code, const std::string& msg) { return ResultPayload<T>(false, code, msg, T{}); }
    static ResultPayload<T> error(const std::string& msg) { return ResultPayload<T>(false, -1, msg, T{}); }

    T getValue() const { return m_value; }

private:
    ResultPayload(bool isOk, int code, const std::string& msg, const T& val)
        : Result(isOk, code, msg), m_value(val) {}

    T m_value;
};



} // namespace video
} // namespace sdk
