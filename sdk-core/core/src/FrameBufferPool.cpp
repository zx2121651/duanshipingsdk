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

void FrameBufferPool::evictToBudget(size_t requiredBytes) {
    // 若即将超载，清除缓存池中不常使用的项 (LRU 简单实现：清除最早进入的)
    // 这里的池子是个 map<string, vector>。为简化，直接遍历各个 vector 直到腾出足够空间。
    while (m_currentVramBytes + requiredBytes > m_maxVramBytes) {
        bool evicted = false;
        for (auto& pair : m_pool) {
            if (!pair.second.empty()) {
                size_t sz = pair.second.front()->getMemorySize();
                pair.second.erase(pair.second.begin()); // 移除最旧的
                if (m_currentVramBytes >= sz) {
                    m_currentVramBytes -= sz;
                }
                evicted = true;
                break; // 每次移除一个再重新检查
            }
        }
        if (!evicted) {
            // 如果池里全空，说明所有内存都被正在使用中，无法强制释放，只能超载警告
            std::cerr << "VRAM Budget OOM Warning! Unable to evict more FrameBuffers." << std::endl;
            break;
        }
    }
}

FrameBufferPtr FrameBufferPool::getFrameBuffer(int width, int height, FBOPrecision precision) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = getKey(width, height, precision);

    if (m_pool.find(key) != m_pool.end() && !m_pool[key].empty()) {
        std::unique_ptr<FrameBuffer> fb = std::move(m_pool[key].back());
        m_pool[key].pop_back();

        size_t sz = fb->getMemorySize();
        if (m_currentVramBytes >= sz) {
            m_currentVramBytes -= sz; // 从缓存池移出，算作 "使用中"，不再计入 pool VRAM budget 管理中。
        }

        // 核心修复：统一生命周期。不论是新创建的还是从池子里拿出来的，
        // 只要离开 shared_ptr 作用域，必定触发自动回归池子的 lambda deleter。
        // 这堵死了 C++ 跨组件传递纹理时由于 manual release 遗忘导致的 FBO 泄露。
        return FrameBufferPtr(fb.release(), [this](FrameBuffer* ptr) {
            this->returnFrameBuffer(ptr);
        });
    }

    // New allocation: check budget first
    size_t newSize = static_cast<size_t>(width) * height * (precision == FBOPrecision::FP16 ? 8 : (precision == FBOPrecision::RGB565 ? 2 : 4));
    evictToBudget(newSize);

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
        size_t sz = fb->getMemorySize();
        evictToBudget(sz);
        m_currentVramBytes += sz;
        m_pool[key].push_back(std::unique_ptr<FrameBuffer>(fb));
    }
}

void FrameBufferPool::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear();
    m_currentVramBytes = 0;
}

} // namespace video
} // namespace sdk
