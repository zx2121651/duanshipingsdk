#include "../include/ThreadCheck.h"
#include <iostream>

namespace sdk {
namespace video {

ThreadCheck::ThreadCheck() : m_bound(false) {}

void ThreadCheck::bind() {
    m_threadId = std::this_thread::get_id();
    m_bound = true;
}

bool ThreadCheck::check(const std::string& msg) const {
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

} // namespace video
} // namespace sdk
