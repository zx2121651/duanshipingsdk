#pragma once
#include "FrameBuffer.h"
#include <map>
#include <vector>
#include <mutex>
#include <string>

namespace sdk {
namespace video {

class FrameBufferPool {
public:
    FrameBufferPool() = default;
    ~FrameBufferPool();

    // 动态嗅探传入的精度策略
    FrameBufferPtr getFrameBuffer(int width, int height, FBOPrecision precision = FBOPrecision::RGBA8888);

    // 为了兼容旧代码的 Boolean 传参，做一个简单的转换
    FrameBufferPtr get(int width, int height, bool isRgb565 = false) {
        return getFrameBuffer(width, height, isRgb565 ? FBOPrecision::RGB565 : FBOPrecision::RGBA8888);
    }

    void returnFrameBuffer(FrameBuffer* fb);
    void release(FrameBufferPtr fb);

    void clear();

    // 设置全局显存熔断上限（默认 256MB）
    void setVramBudget(size_t bytes) { m_maxVramBytes = bytes; }

    FrameBufferPool(const FrameBufferPool&) = delete;
    FrameBufferPool& operator=(const FrameBufferPool&) = delete;

private:
    std::string getKey(int width, int height, FBOPrecision precision) const;
    void evictToBudget(size_t requiredBytes);

    std::mutex m_mutex;
    // 使用 std::list 保留最近使用的顺序，方便做 LRU 驱逐
    std::map<std::string, std::vector<std::unique_ptr<FrameBuffer>>> m_pool;

    size_t m_currentVramBytes = 0;
    size_t m_maxVramBytes = 256 * 1024 * 1024; // 256MB 默认阈值
};

} // namespace video
} // namespace sdk
