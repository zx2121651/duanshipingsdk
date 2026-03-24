#pragma once
#include <thread>
#include <string>
#include <iostream>

namespace sdk {
namespace video {

class ThreadCheck {
public:
    ThreadCheck() : m_bound(false) {}

    void bind() {
        m_threadId = std::this_thread::get_id();
        m_bound = true;
    }

    bool check(const std::string& msg = "Called on incorrect thread") const {
        if (!m_bound) {
            std::cerr << "ThreadCheck Error: Not bound yet! " << msg << std::endl;
            return false;
        }
        if (std::this_thread::get_id() != m_threadId) {
            std::cerr << "ThreadCheck Error: " << msg << std::endl;
            return false;
        }
        return true;
    }

private:
    std::thread::id m_threadId;
    bool m_bound;
};

} // namespace video
} // namespace sdk
