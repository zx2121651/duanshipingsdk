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

    FrameBufferPool(const FrameBufferPool&) = delete;
    FrameBufferPool& operator=(const FrameBufferPool&) = delete;

private:
    std::string getKey(int width, int height, FBOPrecision precision) const;

    std::mutex m_mutex;
    std::map<std::string, std::vector<std::unique_ptr<FrameBuffer>>> m_pool;
};

} // namespace video
} // namespace sdk
