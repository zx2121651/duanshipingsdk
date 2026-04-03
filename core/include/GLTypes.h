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

class Result {
public:
    static Result ok() { return Result(true, 0, ""); }
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
