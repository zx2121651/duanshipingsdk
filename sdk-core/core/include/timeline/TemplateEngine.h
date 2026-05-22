#pragma once
/**
 * TemplateEngine.h
 *
 * 模板一键成片引擎。
 *
 * 职责：
 *   1. 从 JSON 文件或字符串解析 VideoTemplate
 *   2. 将用户素材填入模板 slot，生成完整 Timeline
 *   3. 可选：对生成的 Timeline 做时长校正和转场对齐
 *
 * 用法：
 *   // 1. 加载模板
 *   VideoTemplate tmpl;
 *   if (!TemplateEngine::loadFromFile("assets/templates/romantic.json", tmpl)) { ... }
 *
 *   // 2. 填充素材
 *   tmpl.slots[0].filledPath = "/sdcard/DCIM/clip1.mp4";
 *   tmpl.slots[1].filledPath = "/sdcard/DCIM/clip2.mp4";
 *
 *   // 3. 生成 Timeline
 *   TemplateEngine engine;
 *   auto timeline = engine.apply(tmpl);
 *
 *   // 4. 导出
 *   TimelineExporter exporter;
 *   exporter.exportTo(timeline, config);
 */

#include "VideoTemplate.h"
#include "Timeline.h"
#include <string>
#include <memory>

namespace sdk {
namespace video {
namespace timeline {

class TemplateEngine {
public:
    TemplateEngine() = default;
    ~TemplateEngine() = default;

    // ── 解析 ───────────────────────────────────────────────────────────────

    /**
     * 从 JSON 文件路径加载模板。
     * @param filePath  JSON 文件路径
     * @param outTmpl   输出模板（成功时填充）
     * @return true = 成功；false = 解析失败（查看 getLastError()）
     */
    static bool loadFromFile(const std::string& filePath, VideoTemplate& outTmpl);

    /**
     * 从 JSON 字符串解析模板。
     * @param json    JSON 字符串
     * @param outTmpl 输出模板
     * @return true = 成功
     */
    static bool loadFromString(const std::string& json, VideoTemplate& outTmpl);

    // ── 生成 Timeline ──────────────────────────────────────────────────────

    /**
     * 将已填充素材的模板转换为可导出的 Timeline。
     *
     * 生成规则：
     *   - 为每个 slot 创建 Clip，按 durationMs 顺序排列在 MAIN_VIDEO 轨
     *   - 应用 slot 指定的入场转场
     *   - 若 tmpl.audio.type == "music"，在 AUDIO_ONLY 轨添加音乐 Clip
     *   - 若 tmpl.subtitles 非空，在 SUBTITLE 轨添加 SubtitleClip
     *
     * @param tmpl  已填充 filledPath 的模板（allSlotsFilled() == true）
     * @return      生成的 Timeline；失败返回 nullptr
     */
    std::shared_ptr<Timeline> apply(const VideoTemplate& tmpl);

    /** 最近一次操作的错误信息 */
    const std::string& getLastError() const { return m_lastError; }

    // ── 便捷辅助 ───────────────────────────────────────────────────────────

    /**
     * 扫描目录下所有 .json 文件，批量加载模板列表。
     * @param dirPath   模板目录路径
     * @return          成功解析的模板列表
     */
    static std::vector<VideoTemplate> loadAllFromDirectory(const std::string& dirPath);

private:
    std::string m_lastError;

    // 内联 JSON 解析（无外部依赖，手写有限状态机）
    static bool parseJson(const std::string& json, VideoTemplate& out,
                          std::string& errOut);
};

} // namespace timeline
} // namespace video
} // namespace sdk
