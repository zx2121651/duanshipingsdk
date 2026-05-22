#pragma once
/**
 * StickerRenderer.h
 *
 * 人脸锚点贴纸渲染器 — 将 EffectPlugin 中的 Sticker 层渲染到目标 FBO。
 *
 * 职责：
 *   1. 根据 StickerAnchor.name 从 FaceResult / PoseResult 计算锚点坐标
 *   2. 根据面部尺寸（眼间距）计算贴纸世界尺寸
 *   3. 若 track_rotation = true，计算头部偏转角
 *   4. 在当前绑定的 FBO 上绘制 RGBA 精灵（Alpha Blend）
 *   5. 动画帧：根据当前时间 + EffectLayerDesc.animFps 自动循环帧序列
 *
 * 坐标系约定：
 *   - 关键点归一化坐标 [0,1]，左上角 (0,0)
 *   - GL NDC [-1,1]，变换：ndcX = x*2-1, ndcY = 1-y*2
 *
 * 用法（在 FilterEngine 的 processActiveEffect 中调用）：
 *   StickerRenderer renderer;
 *   renderer.initialize();
 *   renderer.render(layer, plugin, faceResult, frameTimeMs, viewW, viewH);
 */

#include "../GLTypes.h"
#include "../EffectPlugin.h"
#include "FaceLandmarkDetector.h"
#include <cstdint>
#include <string>

#ifdef __ANDROID__
#   include <GLES3/gl3.h>
#elif defined(__APPLE__)
#   include <OpenGLES/ES3/gl.h>
#else
#   include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {
namespace ai {

class StickerRenderer {
public:
    StickerRenderer();
    ~StickerRenderer();

    /** GL 上下文已就绪后调用一次。编译 Shader + 上传 VBO。 */
    Result initialize();

    /**
     * 渲染一个 Sticker 层到当前绑定的 FBO（不自行绑定 FBO，调用方负责）。
     *
     * @param layer       manifest 中该 Sticker 的 EffectLayerDesc
     * @param plugin      已加载的 EffectPlugin（内含纹理缓存）
     * @param face        当前帧平滑后的 FaceResult（若 anchor 是人脸锚点）
     * @param frameTimeMs 当前时间（毫秒），用于动画帧计算
     * @param viewW/H     渲染目标尺寸（像素），用于正确计算宽高比
     */
    void render(const EffectLayerDesc& layer,
                const EffectPlugin&   plugin,
                const FaceResult&     face,
                int64_t               frameTimeMs,
                int                   viewW,
                int                   viewH);

    /** 不依赖人脸关键点的手动位置渲染（用于非脸部锚点）。 */
    void renderAt(GLuint texId,
                  float  centerNdcX, float centerNdcY,
                  float  scaleNdc,   float rotationRad,
                  float  alpha,
                  float  aspectRatio);

    void release();

private:
    // ── Shader ──────────────────────────────────────────────────────────────
    uint32_t m_program   = 0;
    int32_t  m_uTransform= -1; ///< mat3 screen-space transform
    int32_t  m_uAlpha    = -1;
    int32_t  m_uTexture  = -1;

    // ── Geometry: unit quad [-1,1]×[-1,1] ───────────────────────────────────
    uint32_t m_vbo = 0;

    bool m_initialized = false;

    // ── Helpers ─────────────────────────────────────────────────────────────

    /**
     * Resolve anchor name → NDC position + rotation.
     * @param anchorName  e.g. "forehead", "leftEye", "rightEye", "nose", "mouth", "chin"
     * @param face        smoothed face result
     * @param outX/Y      NDC center of anchor
     * @param outRot      head roll angle (radians) if track_rotation is true
     * @param outScale    normalized scale factor based on inter-eye distance
     */
    static bool resolveAnchor(const std::string& anchorName,
                              const FaceResult& face,
                              float& outX, float& outY,
                              float& outRot, float& outScale);

    /** Choose the correct animation frame index from the layer's frames list. */
    static int resolveAnimFrame(const EffectLayerDesc& layer, int64_t frameTimeMs);

    uint32_t compileShader(uint32_t type, const char* src);
    uint32_t linkProgram(uint32_t vert, uint32_t frag);

    static const char* kVert;
    static const char* kFrag;
};

} // namespace ai
} // namespace video
} // namespace sdk
