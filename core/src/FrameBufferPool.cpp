#include "../include/FrameBufferPool.h"
#include <iostream>

namespace sdk {
namespace video {

FrameBufferPool::~FrameBufferPool() {
    clear();
}

FrameBufferPtr FrameBufferPool::getFrameBuffer(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = getKey(width, height);

    std::unique_ptr<FrameBuffer> fbUnique;

    if (m_pool.count(key) && !m_pool[key].empty()) {
        fbUnique = std::move(m_pool[key].back());
        m_pool[key].pop_back();
    } else {
        fbUnique = std::make_unique<FrameBuffer>(width, height);
    }

    // Create shared_ptr with custom deleter that returns the framebuffer to the pool.
    return FrameBufferPtr(fbUnique.release(), [this](FrameBuffer* fb) {
        this->returnFrameBuffer(fb);
    });
}

void FrameBufferPool::returnFrameBuffer(FrameBuffer* fb) {
    if (!fb) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = getKey(fb->width(), fb->height());
    m_pool[key].push_back(std::unique_ptr<FrameBuffer>(fb));
}

void FrameBufferPool::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear(); // This will destruct all unique_ptrs and thus all FrameBuffers.
}

std::string FrameBufferPool::getKey(int width, int height) const {
    return std::to_string(width) + "x" + std::to_string(height);
}

} // namespace video
} // namespace sdk
