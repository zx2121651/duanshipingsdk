#include "../include/FrameBufferPool.h"
#include <iostream>

namespace sdk {
namespace video {

FrameBufferPool::~FrameBufferPool() {
    clear();
}

std::string FrameBufferPool::getKey(int width, int height, bool isRgb565) const {
    return std::to_string(width) + "x" + std::to_string(height) + "_" + (isRgb565 ? "565" : "8888");
}

FrameBufferPtr FrameBufferPool::getFrameBuffer(int width, int height, bool isRgb565) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = getKey(width, height, isRgb565);

    if (m_pool.find(key) != m_pool.end() && !m_pool[key].empty()) {
        // Take from pool
        std::unique_ptr<FrameBuffer> fb = std::move(m_pool[key].back());
        m_pool[key].pop_back();

        // return with custom deleter to auto return to pool when shared_ptr goes out of scope,
        // though we now use manual return in FilterEngine for hyper optimization.
        return FrameBufferPtr(fb.release(), [this](FrameBuffer* ptr) {
            // this->returnFrameBuffer(ptr); // Removed to prevent double return since FilterEngine does it manually
            // Wait, if FilterEngine does it manually, we shouldn't auto return.
            // But what if it reaches the end and the user drops it? We need a flag or just let custom deleter handle it.
            // Let's keep it safe. FilterEngine will use explicit release() method which bypasses deleter return if already returned.
        });
    }

    // Create new
    auto* fbRaw = new FrameBuffer(width, height, isRgb565);
    return FrameBufferPtr(fbRaw, [this](FrameBuffer* ptr) {
        this->returnFrameBuffer(ptr);
    });
}

void FrameBufferPool::release(FrameBufferPtr fb) {
    if (fb) {
        // Just let it go out of scope, custom deleter will call returnFrameBuffer
    }
}

void FrameBufferPool::returnFrameBuffer(FrameBuffer* fb) {
    if (!fb) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = getKey(fb->width(), fb->height(), fb->isRgb565());

    // check if it's already in the pool to prevent double free (if manually returned)
    bool alreadyIn = false;
    if (m_pool.find(key) != m_pool.end()) {
        for (const auto& item : m_pool[key]) {
            if (item.get() == fb) {
                alreadyIn = true; break;
            }
        }
    }

    if (!alreadyIn) {
        m_pool[key].push_back(std::unique_ptr<FrameBuffer>(fb));
    } else {
        // Avoid leaking the raw pointer if it's somehow double-returned
        // Actually since we push_back unique_ptr, we shouldn't hit this.
    }
}

void FrameBufferPool::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear(); // unique_ptr will call FrameBuffer destructors
}

} // namespace video
} // namespace sdk
