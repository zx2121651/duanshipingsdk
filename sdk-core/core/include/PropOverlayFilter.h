#pragma once
#include "Filter.h"
#include <vector>
#include <cstdint>
#include <string>

namespace sdk {
namespace video {

/**
 * PropOverlayFilter — 实时道具/贴纸叠层滤镜
 *
 * 渲染流程（双 draw-call）：
 *   1. 将输入帧透传到输出 FBO（passthrough）
 *   2. 对每个激活道具，开启 Alpha Blend，在输出 FBO 上绘制 RGBA 精灵纹理
 *
 * 最多支持 MAX_PROPS 个同时激活的道具；道具通过 addProp()/removeProp() 管理。
 * 参数更新通过 setParameter(key, value) 实现，key 格式为 "prop<N>.<field>"。
 *
 * 与现有滤镜链完全兼容：只需调用 FilterEngine::addFilter(PropOverlayFilter) 即可。
 */
class PropOverlayFilter : public Filter {
public:
    static constexpr int MAX_PROPS = 8;

    struct Prop {
        uint32_t texId    = 0;      ///< GLES RGBA 纹理 ID（0 表示无效/未激活）
        float    centerX  = 0.0f;   ///< 屏幕中心 X：NDC [-1, 1]，0 = 水平居中
        float    centerY  = 0.0f;   ///< 屏幕中心 Y：NDC [-1, 1]，0 = 垂直居中
        float    scale    = 0.3f;   ///< 尺寸比例（NDC 空间，0.3 ≈ 视口高度的 30%）
        float    rotation = 0.0f;   ///< 旋转角（弧度，逆时针）
        float    alpha    = 1.0f;   ///< 整体透明度 [0, 1]
        bool     active   = false;  ///< 是否渲染
    };

    PropOverlayFilter();
    ~PropOverlayFilter() override = default;

    Result initialize() override;

    /**
     * 添加一个道具。返回分配的槽位索引（0~MAX_PROPS-1），失败返回 -1（槽位已满）。
     */
    int addProp(uint32_t texId, float x = 0.f, float y = 0.f,
                float scale = 0.3f, float rotation = 0.f, float alpha = 1.f);

    /** 停用指定槽位的道具（不释放纹理，纹理生命周期由调用方管理）。 */
    void removeProp(int slot);

    /** 清除所有道具。 */
    void clearProps();

    /** 更新指定槽位的道具参数（槽位越界时静默忽略）。 */
    void updateProp(int slot, const Prop& prop);

    const Prop& getProp(int slot) const;

    // ── Filter 接口覆写（支持通用 setParameter 通道）────────────────────
    // key 格式：
    //   "prop<N>.texId"    float (cast to uint32_t)
    //   "prop<N>.x"        float
    //   "prop<N>.y"        float
    //   "prop<N>.scale"    float
    //   "prop<N>.rotation" float
    //   "prop<N>.alpha"    float
    //   "prop<N>.active"   bool (via int: 1=active, 0=inactive)
    void setParameter(const std::string& key, const std::any& value) override;

protected:
    void onDraw(const Texture& inputTexture, FrameBufferPtr outputFb) override;

    std::string getFragmentShaderSource() const override;
    std::string getFragmentShaderName()   const override { return "prop_passthrough_frag"; }
    std::string getVertexShaderName()     const override { return "prop_passthrough_vert"; }

private:
    // ── Passthrough (step 1) ─────────────────────────────────────────────
    uint32_t m_passthroughProgram = 0;

    // ── Prop sprite (step 2) ─────────────────────────────────────────────
    uint32_t m_propProgram   = 0;
    int32_t  m_uPropCenter   = -1;
    int32_t  m_uPropScale    = -1;
    int32_t  m_uPropRotation = -1;
    int32_t  m_uPropAlpha    = -1;
    int32_t  m_uPropTexture  = -1;

    // ── Geometry: unit quad ─────────────────────────────────────────────
    uint32_t m_quadVbo = 0;

    // ── Prop list ────────────────────────────────────────────────────────
    Prop m_props[MAX_PROPS];

    // ── Shader sources ───────────────────────────────────────────────────
    static const char* kPassthroughVert;
    static const char* kPassthroughFrag;
    static const char* kPropVert;
    static const char* kPropFrag;

    void compilePrograms();
    void drawPassthrough(const Texture& inputTexture, FrameBufferPtr outputFb);
    void drawPropSprites(FrameBufferPtr outputFb);
    uint32_t compileShader(uint32_t type, const char* src);
    uint32_t linkProgram(uint32_t vert, uint32_t frag);
};

} // namespace video
} // namespace sdk
