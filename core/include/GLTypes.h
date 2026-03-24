#pragma once
#include <cstdint>

namespace sdk {
namespace video {

struct Result {
    bool success;
    int code;
    const char* message;

    static Result ok() { return {true, 0, nullptr}; }
    static Result error(int code, const char* msg) { return {false, code, msg}; }
};

struct Texture {
    uint32_t id;
    int width;
    int height;
};

} // namespace video
} // namespace sdk
