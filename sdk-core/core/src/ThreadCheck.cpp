#include "../include/ThreadCheck.h"
#define LOG_TAG "ThreadCheck"
#include "../include/Log.h"

namespace sdk {
namespace video {

ThreadCheck::ThreadCheck() : m_bound(false) {}

void ThreadCheck::bind() {
    m_threadId = std::this_thread::get_id();
    m_bound = true;
}

void ThreadCheck::unbind() {
    m_bound = false;
}

bool ThreadCheck::check(const std::string& msg) const {
    if (!m_bound) {
        LOGE("[ThreadViolation] Not bound yet! %s", msg.c_str());
        return false;
    }
    if (std::this_thread::get_id() != m_threadId) {
        LOGE("[ThreadViolation] %s", msg.c_str());
        return false;
    }
    return true;
}

} // namespace video
} // namespace sdk
