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

    // Returns a FrameBuffer from the pool or creates a new one if necessary.
    // 增加 isRgb565 参数，控制申请半精度带宽砍半版本
    FrameBufferPtr getFrameBuffer(int width, int height, bool isRgb565 = false);

    // 为了兼容 Two-pass 模糊代码中的旧 API 调用
    FrameBufferPtr get(int width, int height, bool isRgb565 = false) {
        return getFrameBuffer(width, height, isRgb565);
    }

    // Returns a FrameBuffer to the pool.  Called automatically by custom deleter.
    void returnFrameBuffer(FrameBuffer* fb);
    void release(FrameBufferPtr fb); // Helper for manual release

    // Clears all framebuffers in the pool.
    void clear();

    // Disallow copying and assignment.
    FrameBufferPool(const FrameBufferPool&) = delete;
    FrameBufferPool& operator=(const FrameBufferPool&) = delete;

private:
    std::string getKey(int width, int height, bool isRgb565) const;

    std::mutex m_mutex;
    std::map<std::string, std::vector<std::unique_ptr<FrameBuffer>>> m_pool;
};

} // namespace video
} // namespace sdk
