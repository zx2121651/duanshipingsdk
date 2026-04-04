#pragma once
#include <cstdint>

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
    // 引入 OpenGL ES 3.1 的扩展，以支持 Compute Shader 和 Image Store
    #include <GLES3/gl31.h>
#endif

namespace sdk {
namespace video {

struct Texture {
    uint32_t id;
    uint32_t width;
    uint32_t height;
};


// ----------------------------------------------------------------------------
// SDK 统一错误码字典 (Unified Error Code Dictionary)
// ----------------------------------------------------------------------------
enum ErrorCode {
    SUCCESS = 0,

    // 初始化错误 (1000 - 1999)
    ERR_INIT_CONTEXT_FAILED = -1001,
    ERR_INIT_SHADER_FAILED = -1002,
    ERR_INIT_OBOE_FAILED = -1003,

    // 渲染错误 (2000 - 2999)
    ERR_RENDER_FBO_ALLOC_FAILED = -2001,
    ERR_RENDER_INVALID_STATE = -2002,
    ERR_RENDER_COMPUTE_NOT_SUPPORTED = -2003,

    // 时间线错误 (3000 - 3999)
    ERR_TIMELINE_NULL = -3001,
    ERR_TIMELINE_TRACK_NOT_FOUND = -3002,
    ERR_TIMELINE_CLIP_NOT_FOUND = -3003
};

class Result {
public:
    static Result ok() { return Result(true, ErrorCode::SUCCESS, ""); }
    static Result error(int code, const std::string& msg) { return Result(false, code, msg); }
    static Result error(const std::string& msg) { return Result(false, -1, msg); }

    bool isOk() const { return m_isOk; }
    int getErrorCode() const { return m_errorCode; }
    std::string getMessage() const { return m_message; }

private:
    Result(bool isOk, int code, const std::string& msg) : m_isOk(isOk), m_errorCode(code), m_message(msg) {}
    bool m_isOk;
    int m_errorCode;
    std::string m_message;
};


} // namespace video
} // namespace sdk
