#pragma once
#include <oboe/Oboe.h>
#include <memory>
#include <vector>
#include <atomic>

// 极简无锁环形缓冲区
class LockFreeRingBuffer {
public:
    LockFreeRingBuffer(size_t capacity) : m_capacity(capacity), m_readPos(0), m_writePos(0) {
        m_buffer.resize(capacity, 0);
    }

    // 在高优先级音频线程调用
    void write(const int16_t* data, int32_t numFrames) {
        size_t writeIdx = m_writePos.load(std::memory_order_relaxed);
        size_t readIdx = m_readPos.load(std::memory_order_acquire);

        size_t available = m_capacity - (writeIdx - readIdx);
        if (numFrames > available) {
            // Buffer overflow, drop data to prevent glitching/blocking
            return;
        }

        for (int32_t i = 0; i < numFrames; ++i) {
            m_buffer[writeIdx % m_capacity] = data[i];
            writeIdx++;
        }
        m_writePos.store(writeIdx, std::memory_order_release);
    }

    // 在 JVM 协程线程调用
    int32_t read(int16_t* outData, int32_t maxFrames) {
        size_t readIdx = m_readPos.load(std::memory_order_relaxed);
        size_t writeIdx = m_writePos.load(std::memory_order_acquire);

        size_t available = writeIdx - readIdx;
        int32_t framesToRead = std::min(static_cast<size_t>(maxFrames), available);

        if (framesToRead == 0) return 0;

        for (int32_t i = 0; i < framesToRead; ++i) {
            outData[i] = m_buffer[readIdx % m_capacity];
            readIdx++;
        }
        m_readPos.store(readIdx, std::memory_order_release);
        return framesToRead;
    }

    void clear() {
        m_readPos.store(0);
        m_writePos.store(0);
    }

private:
    std::vector<int16_t> m_buffer;
    size_t m_capacity;
    std::atomic<size_t> m_readPos;
    std::atomic<size_t> m_writePos;
};


class OboeAudioEngine : public oboe::AudioStreamCallback {
public:
    OboeAudioEngine();
    ~OboeAudioEngine();

    bool start(int sampleRate = 44100);
    void stop();

    // Read PCM data (returns number of bytes read)
    int32_t readPCM(uint8_t* buffer, int32_t numBytes);

    // AudioStreamCallback implementation
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) override;
    void onErrorBeforeClose(oboe::AudioStream *audioStream, oboe::Result error) override;
    void onErrorAfterClose(oboe::AudioStream *audioStream, oboe::Result error) override;

private:
    std::shared_ptr<oboe::AudioStream> m_stream;
    std::unique_ptr<LockFreeRingBuffer> m_ringBuffer;
    bool m_isRecording = false;
};
