#include "OboeAudioEngine.h"
#include <android/log.h>

#define TAG "OboeAudioEngine"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

OboeAudioEngine::OboeAudioEngine() {
    // Allocate ring buffer capable of holding ~1 second of 44100Hz Mono 16-bit PCM
    m_ringBuffer = std::make_unique<LockFreeRingBuffer>(44100 * 2);
}

OboeAudioEngine::~OboeAudioEngine() {
    stop();
}

bool OboeAudioEngine::start(int sampleRate) {
    if (m_isRecording) return true;

    m_sampleRate = sampleRate;
    m_totalFramesRead.store(0);

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input)
           ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
           ->setSharingMode(oboe::SharingMode::Exclusive)
           ->setFormat(oboe::AudioFormat::I16)
           ->setChannelCount(oboe::ChannelCount::Mono)
           ->setSampleRate(sampleRate)
           ->setCallback(this)
           ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium); // 启用高质量重采样

    oboe::Result result = builder.openStream(m_stream);
    if (result != oboe::Result::OK) {
        LOGE("Failed to open oboe stream. Error: %s", oboe::convertToText(result));
        return false;
    }

    m_ringBuffer->clear();

    result = m_stream->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("Failed to start oboe stream. Error: %s", oboe::convertToText(result));
        m_stream->close();
        m_stream.reset();
        return false;
    }

    m_isRecording = true;
    LOGI("Oboe Audio Stream started. Target Sample Rate: %d, Actual: %d", sampleRate, m_stream->getSampleRate());
    return true;
}

void OboeAudioEngine::stop() {
    if (!m_isRecording || !m_stream) return;

    m_stream->requestStop();
    m_stream->close();
    m_stream.reset();
    m_isRecording = false;
    LOGI("Oboe Audio Stream stopped.");
}

int32_t OboeAudioEngine::readPCM(uint8_t* buffer, int32_t numBytes) {
    if (!m_isRecording || !m_ringBuffer) return 0;

    // We store int16_t in the ring buffer.
    int32_t maxFrames = numBytes / sizeof(int16_t);
    int16_t* outData = reinterpret_cast<int16_t*>(buffer);

    int32_t framesRead = m_ringBuffer->read(outData, maxFrames);
    return framesRead * sizeof(int16_t); // Return bytes read
}

int64_t OboeAudioEngine::getAudioTimeNs() const {
    if (m_sampleRate == 0) return 0;
    // ns = (frames / sampleRate) * 10^9
    // To prevent overflow before division:
    return (m_totalFramesRead.load(std::memory_order_relaxed) * 1000000000LL) / m_sampleRate;
}

// ---------------------------------------------------------
// 高优先级音频回调线程 (绝对禁止锁或内存分配)
// ---------------------------------------------------------
oboe::DataCallbackResult OboeAudioEngine::onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) {
    if (audioData == nullptr || numFrames <= 0) {
        return oboe::DataCallbackResult::Continue;
    }

    // 此时的 data 已经被 Oboe 的内部 Resampler 处理成我们要求的 44100Hz (如果设备原生不支持)
    const int16_t* pcmData = static_cast<const int16_t*>(audioData);

    // 无锁推入缓冲区
    m_ringBuffer->write(pcmData, numFrames);

    // 更新主时钟的帧计数（以此作为全链路的绝对时间参考）
    m_totalFramesRead.fetch_add(numFrames, std::memory_order_relaxed);

    return oboe::DataCallbackResult::Continue;
}

void OboeAudioEngine::onErrorBeforeClose(oboe::AudioStream *audioStream, oboe::Result error) {
    LOGE("Oboe Error before close: %s", oboe::convertToText(error));
}

void OboeAudioEngine::onErrorAfterClose(oboe::AudioStream *audioStream, oboe::Result error) {
    LOGE("Oboe Error after close: %s", oboe::convertToText(error));
}
