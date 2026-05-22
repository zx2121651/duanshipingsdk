#pragma once
#include <thread>
#include <string>

namespace sdk {
namespace video {

class ThreadCheck {
public:
    ThreadCheck();

    void bind();
    void unbind();
    bool check(const std::string& msg = "Called on incorrect thread") const;

private:
    std::thread::id m_threadId;
    bool m_bound;
};

} // namespace video
} // namespace sdk
