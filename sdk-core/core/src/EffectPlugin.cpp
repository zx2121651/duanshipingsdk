/**
 * EffectPlugin.cpp — 特效插件系统实现
 *
 * JSON 解析采用手写轻量解析器，避免引入第三方库依赖。
 * 生产环境可替换为 nlohmann/json 或 rapidjson。
 */
#include "../include/EffectPlugin.h"
#define LOG_TAG "EffectPlugin"
#include "../include/Log.h"

#ifdef __ANDROID__
#   include <GLES3/gl3.h>
#elif defined(__APPLE__)
#   include <OpenGLES/ES3/gl.h>
#else
#   include <GLES3/gl3.h>
#endif

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace sdk {
namespace video {

// ===========================================================================
// EffectPlugin
// ===========================================================================
EffectPlugin::EffectPlugin(EffectPluginDesc desc) : m_desc(std::move(desc)) {}

GLuint EffectPlugin::getStickerTexture(const std::string& path) const {
    auto it = m_textures.find(path);
    return (it != m_textures.end()) ? it->second : 0;
}
void EffectPlugin::setStickerTexture(const std::string& path, GLuint id) {
    m_textures[path] = id;
}

// ===========================================================================
// 轻量 JSON 解析工具（仅覆盖 manifest.json 所需字段）
// ===========================================================================
namespace {

struct JsonVal {
    enum Type { Null, Str, Num, Bool, Obj, Arr } type = Null;
    std::string                             str;
    double                                  num = 0.0;
    bool                                    b   = false;
    std::unordered_map<std::string, std::shared_ptr<JsonVal>> obj;
    std::vector<std::shared_ptr<JsonVal>>                     arr;
};

static void skipWS(const char*& p) {
    while (*p && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) ++p;
}

static std::string parseStr(const char*& p) {
    if (*p != '"') return {};
    ++p;
    std::string out;
    while (*p && *p != '"') {
        if (*p == '\\') { ++p; out += *p; } else out += *p;
        ++p;
    }
    if (*p == '"') ++p;
    return out;
}

static JsonVal parseValue(const char*& p);

static JsonVal parseObject(const char*& p) {
    JsonVal v; v.type = JsonVal::Obj;
    if (*p == '{') ++p;
    while (true) {
        skipWS(p);
        if (*p == '}') { ++p; break; }
        if (*p == ',') { ++p; continue; }
        std::string key = parseStr(p);
        skipWS(p);
        if (*p == ':') ++p;
        skipWS(p);
        v.obj[key] = std::make_shared<JsonVal>(parseValue(p));
    }
    return v;
}

static JsonVal parseArray(const char*& p) {
    JsonVal v; v.type = JsonVal::Arr;
    if (*p == '[') ++p;
    while (true) {
        skipWS(p);
        if (*p == ']') { ++p; break; }
        if (*p == ',') { ++p; continue; }
        v.arr.push_back(std::make_shared<JsonVal>(parseValue(p)));
    }
    return v;
}

static JsonVal parseValue(const char*& p) {
    skipWS(p);
    if (*p == '{') return parseObject(p);
    if (*p == '[') return parseArray(p);
    if (*p == '"') { JsonVal v; v.type=JsonVal::Str; v.str=parseStr(p); return v; }
    if (*p=='t'||*p=='f') {
        JsonVal v; v.type=JsonVal::Bool;
        v.b = (*p=='t');
        while (*p && *p!=',' && *p!='}' && *p!=']') ++p;
        return v;
    }
    if (*p=='n') { while(*p&&*p!=','&&*p!='}'&&*p!=']')++p; return JsonVal{}; }
    // number
    JsonVal v; v.type=JsonVal::Num;
    char* end; v.num = std::strtod(p, &end); p = end;
    return v;
}

static std::string getStr(const JsonVal& v) { return v.type==JsonVal::Str ? v.str : ""; }
static float       getF  (const JsonVal& v) { return v.type==JsonVal::Num ? (float)v.num : 0.f; }

static EffectLayerType parseLayerType(const std::string& s) {
    if (s=="color_filter") return EffectLayerType::ColorFilter;
    if (s=="sticker")      return EffectLayerType::Sticker;
    if (s=="beauty")       return EffectLayerType::Beauty;
    if (s=="makeup")       return EffectLayerType::Makeup;
    if (s=="segmentation") return EffectLayerType::Segmentation;
    if (s=="particle")     return EffectLayerType::Particle;
    return EffectLayerType::Unknown;
}

} // anonymous namespace

// ===========================================================================
// EffectPluginDesc 解析
// ===========================================================================
EffectPluginDesc EffectPluginManager::parseManifest(const std::string& json) const {
    EffectPluginDesc desc;
    const char* p = json.c_str();
    JsonVal root = parseValue(p);
    if (root.type != JsonVal::Obj) return desc;

    auto g = [&](const std::string& key) -> const JsonVal& {
        static JsonVal empty;
        auto it = root.obj.find(key);
        return it != root.obj.end() ? *it->second : empty;
    };

    desc.id            = getStr(g("id"));
    desc.name          = getStr(g("name"));
    desc.version       = getStr(g("version"));
    desc.effectType    = getStr(g("type"));
    if (g("schema_version").type == JsonVal::Str)
        desc.schemaVersion = getStr(g("schema_version"));

    auto& layers = g("layers");
    if (layers.type == JsonVal::Arr) {
        for (auto& lvPtr : layers.arr) {
            auto& lv = *lvPtr;
            if (lv.type != JsonVal::Obj) continue;
            EffectLayerDesc ld;
            auto getL = [&](const std::string& k) -> const JsonVal& {
                static JsonVal empty2;
                auto it2 = lv.obj.find(k);
                return it2 != lv.obj.end() ? *it2->second : empty2;
            };
            ld.type      = parseLayerType(getStr(getL("type")));
            ld.intensity = getL("intensity").type==JsonVal::Num ? getF(getL("intensity")) : 1.f;

            if (ld.type == EffectLayerType::ColorFilter)
                ld.assetPath = getStr(getL("lut"));
            else if (ld.type == EffectLayerType::Sticker) {
                ld.assetPath = getStr(getL("texture"));

                // ── anchor ─────────────────────────────────────────────────
                ld.stickerAnchor.name    = getStr(getL("anchor"));
                ld.stickerAnchor.scale   = getF(getL("scale"));
                ld.stickerAnchor.offsetX = getF(getL("offset_x"));
                ld.stickerAnchor.offsetY = getF(getL("offset_y"));
                if (getL("track_rotation").type == JsonVal::Bool)
                    ld.stickerAnchor.trackRotation = getL("track_rotation").b;

                std::string ancType = getStr(getL("anchor_type"));
                if (ancType == "body")
                    ld.stickerAnchor.anchorType = StickerAnchorType::Body;
                else if (ancType == "fixed") {
                    ld.stickerAnchor.anchorType = StickerAnchorType::Fixed;
                    ld.stickerAnchor.fixedNdcX  = getF(getL("fixed_x"));
                    ld.stickerAnchor.fixedNdcY  = getF(getL("fixed_y"));
                } else {
                    ld.stickerAnchor.anchorType = StickerAnchorType::Face;
                }

                // ── animated frames ────────────────────────────────────────
                auto& framesVal = getL("frames");
                if (framesVal.type == JsonVal::Arr) {
                    for (auto& fvPtr : framesVal.arr) {
                        auto& fv = *fvPtr;
                        if (fv.type == JsonVal::Str)
                            ld.animFrames.push_back(fv.str);
                    }
                }
                if (getL("frame_rate").type == JsonVal::Num)
                    ld.animFps = getF(getL("frame_rate"));
                if (getL("loop").type == JsonVal::Bool)
                    ld.animLoop = getL("loop").b;

                // ── gesture trigger ────────────────────────────────────────
                ld.gestureTrigger = getStr(getL("gesture"));
            } else if (ld.type == EffectLayerType::Beauty) {
                ld.eyeScale   = getF(getL("eyeScale"));
                ld.faceSlim   = getF(getL("faceSlim"));
                ld.noseSlim   = getF(getL("noseSlim"));
                ld.foreheadUp = getF(getL("foreheadUp"));
                ld.chinV      = getF(getL("chinV"));
            } else if (ld.type == EffectLayerType::Makeup) {
                auto fillColor = [&](const std::string& key, float* rgba) {
                    auto& cv = getL(key);
                    if (cv.type==JsonVal::Arr && cv.arr.size()>=4) {
                        for (int i=0;i<4;++i) rgba[i] = getF(*cv.arr[i]);
                    }
                };
                fillColor("lipColor",       ld.lipColor);
                fillColor("blushColor",     ld.blushColor);
                fillColor("eyeshadowColor", ld.eyeshadowColor);
                ld.highlightIntensity = getF(getL("highlight"));
                ld.contourIntensity   = getF(getL("contour"));
            }
            desc.layers.push_back(std::move(ld));
        }
    }
    return desc;
}

// ===========================================================================
// EffectPluginManager
// ===========================================================================
EffectPluginManager::EffectPluginManager() {
    // 默认资产加载器：std::ifstream
    m_assetLoader = [](const std::string& path) -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) return {};
        return { std::istreambuf_iterator<char>(f), {} };
    };
}

std::vector<uint8_t> EffectPluginManager::loadAsset(const std::string& path) {
    return m_assetLoader ? m_assetLoader(path) : std::vector<uint8_t>{};
}

std::string EffectPluginManager::loadEffect(const std::string& effectRoot) {
    std::string manifestPath = effectRoot + "/manifest.json";
    auto data = loadAsset(manifestPath);
    if (data.empty()) {
        LOGE("EffectPluginManager: manifest not found: %s", manifestPath.c_str());
        return {};
    }
    std::string json(data.begin(), data.end());
    return loadEffectFromJSON(json, effectRoot);
}

std::string EffectPluginManager::loadEffectFromJSON(
    const std::string& json, const std::string& effectRoot)
{
    auto desc = parseManifest(json);
    if (desc.id.empty()) {
        LOGE("EffectPluginManager: parse failed or id empty");
        return {};
    }
    auto plugin = std::make_shared<EffectPlugin>(desc);

    // 预加载贴纸纹理（静态 + 动画帧序列）
    for (auto& layer : desc.layers) {
        if (layer.type != EffectLayerType::Sticker) continue;
        // static / first-frame texture
        if (!layer.assetPath.empty()) {
            GLuint texId = loadTexture(effectRoot, layer.assetPath);
            if (texId) plugin->setStickerTexture(layer.assetPath, texId);
        }
        // animated frame textures
        for (auto& framePath : layer.animFrames) {
            if (!framePath.empty()) {
                GLuint texId = loadTexture(effectRoot, framePath);
                if (texId) plugin->setStickerTexture(framePath, texId);
            }
        }
    }
    plugin->markReady();
    m_effects[desc.id] = plugin;
    LOGI("EffectPluginManager: loaded effect '%s' (%zu layers)",
         desc.id.c_str(), desc.layers.size());
    return desc.id;
}

GLuint EffectPluginManager::loadTexture(const std::string& root, const std::string& rel) {
    std::string path = root + "/" + rel;
    auto data = loadAsset(path);
    if (data.empty()) return 0;

    // 简单 RGBA 纹理上传（生产环境应接入 STB Image 或平台 decoder）
    // 此处仅创建占位纹理
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // 1×1 白色占位
    uint8_t white[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    LOGI("EffectPluginManager: texture placeholder created for %s", rel.c_str());
    return tex;
}

void EffectPluginManager::activateEffect(const std::string& id) {
    m_activeEffectId = id;
    LOGI("EffectPluginManager: active effect -> '%s'", id.c_str());
}

void EffectPluginManager::deactivateAll() { m_activeEffectId.clear(); }

const EffectPlugin* EffectPluginManager::getActiveEffect() const {
    if (m_activeEffectId.empty()) return nullptr;
    auto it = m_effects.find(m_activeEffectId);
    return it != m_effects.end() ? it->second.get() : nullptr;
}

EffectPlugin* EffectPluginManager::getActiveEffect() {
    if (m_activeEffectId.empty()) return nullptr;
    auto it = m_effects.find(m_activeEffectId);
    return it != m_effects.end() ? it->second.get() : nullptr;
}

void EffectPluginManager::unloadEffect(const std::string& id) {
    if (m_activeEffectId == id) m_activeEffectId.clear();
    m_effects.erase(id);
}

void EffectPluginManager::unloadAll() {
    m_activeEffectId.clear();
    m_effects.clear();
}

std::vector<std::string> EffectPluginManager::getEffectIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_effects.size());
    for (auto& [id, _] : m_effects) ids.push_back(id);
    return ids;
}

} // namespace video
} // namespace sdk
