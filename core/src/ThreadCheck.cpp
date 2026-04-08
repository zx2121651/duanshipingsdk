
#include "../include/ThreadCheck.h"
#include <iostream>

namespace sdk {
namespace video {

ThreadCheck::ThreadCheck() : m_threadId(std::this_thread::get_id()), m_isBound(false) {}

void ThreadCheck::bind() {
    m_threadId = std::this_thread::get_id();
    m_isBound = true;
}

bool ThreadCheck::check(const std::string& message) const {
    if (m_isBound && std::this_thread::get_id() != m_threadId) {
        std::cerr << "ThreadCheck failed: " << message << std::endl;
        return false;
    }
    return true;
}

}
}
