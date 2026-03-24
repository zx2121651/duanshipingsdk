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
    FrameBufferPtr getFrameBuffer(int width, int height);

    // Returns a FrameBuffer to the pool.  Called automatically by custom deleter.
    void returnFrameBuffer(FrameBuffer* fb);

    // Clears all framebuffers in the pool.
    void clear();

    // Disallow copying and assignment.
    FrameBufferPool(const FrameBufferPool&) = delete;
    FrameBufferPool& operator=(const FrameBufferPool&) = delete;

private:
    std::string getKey(int width, int height) const;

    std::mutex m_mutex;
    std::map<std::string, std::vector<std::unique_ptr<FrameBuffer>>> m_pool;
};

} // namespace video
} // namespace sdk
