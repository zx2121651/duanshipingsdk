#include "../include/FrameBufferPool.h"
#include <iostream>

namespace sdk {
namespace video {

FrameBufferPool::~FrameBufferPool() {
    clear();
}

std::string FrameBufferPool::getKey(int width, int height, FBOPrecision precision) const {
    std::string pStr = (precision == FBOPrecision::FP16) ? "fp16" :
                       (precision == FBOPrecision::RGB565) ? "565" : "8888";
    return std::to_string(width) + "x" + std::to_string(height) + "_" + pStr;
}

FrameBufferPtr FrameBufferPool::getFrameBuffer(int width, int height, FBOPrecision precision) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = getKey(width, height, precision);

    if (m_pool.find(key) != m_pool.end() && !m_pool[key].empty()) {
        std::unique_ptr<FrameBuffer> fb = std::move(m_pool[key].back());
        m_pool[key].pop_back();

        return FrameBufferPtr(fb.release(), [](FrameBuffer* ptr) {});
    }

    auto* fbRaw = new FrameBuffer(width, height, precision);
    return FrameBufferPtr(fbRaw, [this](FrameBuffer* ptr) {
        this->returnFrameBuffer(ptr);
    });
}

void FrameBufferPool::release(FrameBufferPtr fb) {}

void FrameBufferPool::returnFrameBuffer(FrameBuffer* fb) {
    if (!fb) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = getKey(fb->width(), fb->height(), fb->precision());

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
    }
}

void FrameBufferPool::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear();
}

} // namespace video
} // namespace sdk
