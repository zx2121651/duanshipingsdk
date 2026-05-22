#pragma once
/**
 * AutoSubtitleEngine.h
 *
 * 离线语音识别 (ASR) → 字幕片段生成。
 *
 * 架构：
 *   - C++ 接口层（AutoSubtitleEngine）定义通用 API
 *   - 具体实现由平台层注入（Android: VoskRecognizer / WhisperJNI；
 *     其他平台: whisper.cpp 直接调用）
 *   - 当无实现时退化为 stub（返回空列表，不崩溃）
 *
 * 输出格式：
 *   std::vector<SubtitleSegment> — 每个 segment 含文字、开始/结束时间（ns）
 *
 * 用法：
 *   auto engine = AutoSubtitleEngine::createDefault();
 *   engine->setLanguage("zh");
 *   auto segments = engine->recognize("/path/to/audio.pcm", durationNs);
 *   for (auto& s : segments) {
 *       auto clip = std::make_shared<SubtitleClip>(s.id, s.text);
 *       clip->setTimelineIn(s.startNs);
 *       clip->setSourceDuration(s.endNs - s.startNs);
 *       subtitleTrack->addClip(clip);
 *   }
 */

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// SubtitleSegment — 单条识别结果
// ---------------------------------------------------------------------------
struct SubtitleSegment {
    std::string id;           ///< 自动生成的唯一 ID（"sub_0", "sub_1", …）
    std::string text;         ///< 识别到的文字内容
    int64_t     startNs  = 0; ///< 字幕开始时间（相对于音频起点，纳秒）
    int64_t     endNs    = 0; ///< 字幕结束时间（纳秒）
    float       confidence = 0.f; ///< 识别置信度 [0,1]
};

// ---------------------------------------------------------------------------
// AutoSubtitleEngine — ASR 接口
// ---------------------------------------------------------------------------
class AutoSubtitleEngine : public std::enable_shared_from_this<AutoSubtitleEngine> {
public:
    virtual ~AutoSubtitleEngine() = default;

    /**
     * 创建默认引擎实例。
     * - 若平台实现已注册（通过 registerFactory()），则使用平台实现。
     * - 否则返回 stub 实现（recognize 返回空列表）。
     */
    static std::shared_ptr<AutoSubtitleEngine> createDefault();

    /**
     * 注册平台具体实现工厂（从 JNI / Objective-C 桥调用）。
     * @param factory  返回 AutoSubtitleEngine 实例的工厂函数
     */
    static void registerFactory(
        std::function<std::shared_ptr<AutoSubtitleEngine>()> factory);

    // ── 配置 ───────────────────────────────────────────────────────────────

    /**
     * 设置识别语言（BCP-47 格式，如 "zh"、"en"、"ja"）。
     * 默认 "zh"。
     */
    virtual void setLanguage(const std::string& langCode) { m_language = langCode; }
    const std::string& getLanguage() const { return m_language; }

    /**
     * 设置模型路径（Vosk 模型目录 或 Whisper .bin 文件）。
     * 若未设置，引擎将使用内置 / 平台默认模型。
     */
    virtual void setModelPath(const std::string& path) { m_modelPath = path; }
    const std::string& getModelPath() const { return m_modelPath; }

    // ── 识别 ───────────────────────────────────────────────────────────────

    /**
     * 同步识别 PCM 音频文件，返回字幕列表。
     *
     * @param audioPath    PCM/WAV 文件路径（16kHz, 16-bit mono 为标准格式）
     * @param durationNs   音频总时长（纳秒），用于对齐时间戳
     * @return             识别到的字幕段落列表（按 startNs 升序排列）
     *
     * 注意：本接口为阻塞调用，建议在后台线程执行。
     */
    virtual std::vector<SubtitleSegment> recognize(
        const std::string& audioPath, int64_t durationNs) = 0;

    /**
     * 异步识别版本（内部启动线程）。
     * 完成后调用 onDone 回调（可能从后台线程调用）。
     *
     * @param audioPath   音频路径
     * @param durationNs  音频时长（纳秒）
     * @param onDone      完成回调：(segments, errorMsg)；errorMsg 非空表示失败
     */
    virtual void recognizeAsync(
        const std::string& audioPath,
        int64_t durationNs,
        std::function<void(std::vector<SubtitleSegment>, std::string)> onDone);

    /**
     * 取消正在进行的异步识别。
     */
    virtual void cancel() {}

    /** 最近一次操作的错误信息。 */
    const std::string& getLastError() const { return m_lastError; }

protected:
    std::string m_language  = "zh";
    std::string m_modelPath;
    std::string m_lastError;
};

// ---------------------------------------------------------------------------
// StubAutoSubtitleEngine — 无模型时的空实现
// ---------------------------------------------------------------------------
class StubAutoSubtitleEngine : public AutoSubtitleEngine {
public:
    std::vector<SubtitleSegment> recognize(
        const std::string& /*audioPath*/, int64_t /*durationNs*/) override {
        return {};
    }
};

} // namespace ai
} // namespace video
} // namespace sdk
