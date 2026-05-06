#pragma once
/**
 * EffectPlugin.h
 *
 * 特效插件系统 — 对标抖音 Effect SDK 的特效包（.bundle）机制。
 *
 * 特效包目录结构：
 *   effects/
 *     my_effect/
 *       manifest.json          ← 特效描述文件
 *       textures/              ← 贴图资源
 *         sticker_0.png
 *       luts/                  ← 颜色滤镜 LUT
 *         color.cube
 *       models/                ← AI 模型
 *         landmark.tflite
 *
 * manifest.json 格式：
 * {
 *   "id": "my_effect",
 *   "name": "我的特效",
 *   "version": "1.0",
 *   "type": "face_sticker",     // face_sticker | color_filter | segmentation | beauty
 *   "layers": [
 *     {
 *       "type": "color_filter",
 *       "lut": "luts/color.cube",
 *       "intensity": 0.8
 *     },
 *     {
 *       "type": "sticker",
 *       "texture": "textures/sticker_0.png",
 *       "anchor": "forehead",   // forehead | leftEye | rightEye | nose | mouth | chin
 *       "scale": 0.25,
 *       "offset_x": 0.0,
 *       "offset_y": -0.05,
 *       "track_rotation": true
 *     },
 *     {
 *       "type": "beauty",
 *       "eyeScale": 0.3,
 *       "faceSlim": 0.2,
 *       "lipColor": [0.8, 0.1, 0.2, 0.6]
 *     }
 *   ]
 * }
 */

#include "GLTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace sdk {
namespace video {

// ---------------------------------------------------------------------------
// 特效层描述
// ---------------------------------------------------------------------------
enum class EffectLayerType {
    Unknown,
    ColorFilter,   ///< LUT 颜色滤镜
    Sticker,       ///< 人脸贴纸（绑定关键点）
    Beauty,        ///< 磨皮/美白/大眼/瘦脸
    Makeup,        ///< 美妆（口红/腮红/眼影）
    Segmentation,  ///< 人像分割（背景替换）
    Particle,      ///< 粒子效果（几何着色器）
};

struct StickerAnchor {
    std::string name; ///< "forehead","leftEye","rightEye","nose","mouth","chin"
    float offsetX = 0.f;
    float offsetY = 0.f;
    float scale   = 0.2f;
    bool  trackRotation = true;
};

struct EffectLayerDesc {
    EffectLayerType type      = EffectLayerType::Unknown;
    std::string     assetPath; ///< 相对于 effectRoot 的资源路径
    float           intensity = 1.f;

    // Sticker 特有
    StickerAnchor stickerAnchor;

    // Beauty 特有
    float eyeScale   = 0.f;
    float faceSlim   = 0.f;
    float noseSlim   = 0.f;
    float foreheadUp = 0.f;
    float chinV      = 0.f;

    // Makeup 特有
    float lipColor[4]       = {0.f, 0.f, 0.f, 0.f}; // RGBA + intensity
    float blushColor[4]     = {0.f, 0.f, 0.f, 0.f};
    float eyeshadowColor[4] = {0.f, 0.f, 0.f, 0.f};
    float highlightIntensity= 0.f;
    float contourIntensity  = 0.f;
};

// ---------------------------------------------------------------------------
// 特效包描述
// ---------------------------------------------------------------------------
struct EffectPluginDesc {
    std::string id;
    std::string name;
    std::string version;
    std::string effectType; ///< "face_sticker" | "color_filter" | "beauty" etc.
    std::vector<EffectLayerDesc> layers;
};

// ---------------------------------------------------------------------------
// EffectPlugin — 单个已加载特效
// ---------------------------------------------------------------------------
class EffectPlugin {
public:
    explicit EffectPlugin(EffectPluginDesc desc);
    ~EffectPlugin() = default;

    const EffectPluginDesc& getDesc() const { return m_desc; }
    const std::string&      getId()   const { return m_desc.id; }

    /** 特效是否已完全加载（纹理/模型就绪） */
    bool isReady() const { return m_ready; }

    /** 全局强度覆盖 [0,1] */
    void  setIntensity(float v) { m_globalIntensity = v; }
    float getIntensity()  const { return m_globalIntensity; }

    /** 已加载的 GL 纹理 ID（贴纸纹理缓存） */
    GLuint getStickerTexture(const std::string& assetPath) const;
    void   setStickerTexture(const std::string& assetPath, GLuint texId);

    void markReady() { m_ready = true; }

private:
    EffectPluginDesc m_desc;
    bool  m_ready           = false;
    float m_globalIntensity = 1.f;
    std::unordered_map<std::string, GLuint> m_textures;
};

// ---------------------------------------------------------------------------
// EffectPluginManager — 特效包加载 / 激活 / 卸载
// ---------------------------------------------------------------------------
class EffectPluginManager {
public:
    using AssetLoader = std::function<
        std::vector<uint8_t>(const std::string& path)>;

    EffectPluginManager();
    ~EffectPluginManager() = default;

    /**
     * 设置资产加载回调（平台层实现，Android 通过 AssetManager，iOS 通过 Bundle）。
     * 默认使用 std::ifstream。
     */
    void setAssetLoader(AssetLoader loader) { m_assetLoader = std::move(loader); }

    /**
     * 从目录加载特效包（解析 manifest.json + 预加载纹理）。
     * @param effectRoot  特效根目录路径
     * @return 特效 ID（失败返回空字符串）
     */
    std::string loadEffect(const std::string& effectRoot);

    /**
     * 从内存 JSON 加载特效包。
     * @param manifestJson  manifest.json 内容
     * @param effectRoot    资产根路径（用于解析相对路径）
     */
    std::string loadEffectFromJSON(const std::string& manifestJson,
                                   const std::string& effectRoot);

    /** 激活特效（每次只激活一个，传空字符串 = 关闭所有） */
    void activateEffect(const std::string& effectId);
    void deactivateAll();

    /** 获取当前激活特效（nullptr = 无） */
    const EffectPlugin* getActiveEffect() const;
    EffectPlugin*       getActiveEffect();

    /** 卸载（释放 GL 资源） */
    void unloadEffect(const std::string& effectId);
    void unloadAll();

    /** 已注册的所有特效 ID */
    std::vector<std::string> getEffectIds() const;

private:
    std::unordered_map<std::string, std::shared_ptr<EffectPlugin>> m_effects;
    std::string  m_activeEffectId;
    AssetLoader  m_assetLoader;

    EffectPluginDesc parseManifest(const std::string& json) const;
    GLuint loadTexture(const std::string& assetRoot, const std::string& relPath);
    std::vector<uint8_t> loadAsset(const std::string& path);
};

} // namespace video
} // namespace sdk
