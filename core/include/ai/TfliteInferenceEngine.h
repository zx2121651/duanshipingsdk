#pragma once
/**
 * TfliteInferenceEngine.h
 *
 * 通用 TensorFlow Lite 推理封装层。
 *
 * 职责：
 *   - 加载 .tflite 模型（路径 或 内存 buffer 均支持）
 *   - 管理 Interpreter 生命周期
 *   - 自动选择 GPU Delegate（Android）或 CPU fallback
 *   - 执行推理：输入 RGBA 原始像素 → 输出 float[H×W] mask
 *   - 将 mask 上传为 GL_R8 纹理（供下游 SegmentationFilter 读取）
 *
 * 编译守护：
 *   HAS_TFLITE  — 完整 TFLite 实现
 *   否则        — stub（所有方法返回明确错误码，不崩溃）
 */

#include "../GLTypes.h"

#ifdef __ANDROID__
#   include <GLES3/gl3.h>
#elif defined(__APPLE__)
#   include <OpenGLES/ES3/gl.h>
#else
#   include <GLES3/gl3.h>
#endif

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// 推理结果
// ---------------------------------------------------------------------------
struct InferenceResult {
    bool    success = false;
    GLuint  maskTextureId = 0;  // GL_R8 纹理，值域 [0,1]，1=前景（人像）
    int     maskWidth  = 0;
    int     maskHeight = 0;
    float   inferenceTimeMs = 0.0f;
    std::string errorMessage;
};

// ---------------------------------------------------------------------------
// TfliteInferenceEngine
// ---------------------------------------------------------------------------
class TfliteInferenceEngine {
public:
    TfliteInferenceEngine();
    ~TfliteInferenceEngine();

    // 禁止拷贝
    TfliteInferenceEngine(const TfliteInferenceEngine&) = delete;
    TfliteInferenceEngine& operator=(const TfliteInferenceEngine&) = delete;

    /**
     * Delegate 偏好提示（由 Kotlin TfliteDelegateStrategy 设置，在 loadModel() 之前调用）。
     *   GPU=0, NNAPI=1, XNNPACK=2, CPU=3
     */
    enum class DelegateHint { GPU = 0, NNAPI = 1, XNNPACK = 2, CPU = 3 };

    /** 设置 delegate 偏好；必须在 loadModel() 之前调用。默认 GPU（回退链：GPU→CPU）。 */
    void setDelegateHint(DelegateHint hint) { m_delegateHint = hint; }
    DelegateHint getDelegateHint() const    { return m_delegateHint; }

    /**
     * 从文件路径加载模型。
     * @param modelPath  .tflite 文件的绝对路径（Android 侧通过 AssetManager 提前解压）
     * @return true = 成功；false = 失败（查看 getLastError()）
     */
    bool loadModel(const std::string& modelPath);

    /**
     * 从内存 buffer 加载模型（适合从 Android assets 直接读入的场景）。
     * buffer 生命周期由调用方管理，必须在 runInference() 期间保持有效。
     */
    bool loadModelFromBuffer(const void* modelData, size_t modelSize);

    /**
     * 执行推理。
     *
     * 内部流程：
     *   1. 将 inputPixels (RGBA, inputW×inputH) resize 到模型输入尺寸
     *   2. Interpreter::Invoke()
     *   3. 将输出 float[modelH×modelW] 上传为 GL_R8 纹理
     *
     * @param inputPixels  RGBA 字节数组，大小 = inputW × inputH × 4
     * @param inputW       输入图像宽度（像素）
     * @param inputH       输入图像高度（像素）
     * @return InferenceResult（含 maskTextureId）
     *
     * @note 必须在 GL 线程调用（上传纹理需要 GL 上下文）
     */
    InferenceResult runInference(const uint8_t* inputPixels, int inputW, int inputH);

    /**
     * 查询模型期望的输入尺寸。
     * loadModel() 成功后有效。
     */
    int getInputWidth()  const { return m_inputW; }
    int getInputHeight() const { return m_inputH; }

    /** 是否已成功加载模型 */
    bool isLoaded() const { return m_loaded; }

    /** 最近一次操作的错误描述 */
    const std::string& getLastError() const { return m_lastError; }

    /** 释放 GL 资源（maskTexture），必须在 GL 线程调用 */
    void releaseGLResources();

    /** 完整释放（包含 Interpreter + GL 资源） */
    void release();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    int  m_inputW    = 256;
    int  m_inputH    = 256;
    bool m_loaded    = false;
    std::string m_lastError;
    DelegateHint m_delegateHint = DelegateHint::GPU;

    GLuint m_maskTextureId = 0;
    bool   m_maskTexInited = false;

    void ensureMaskTexture(int w, int h);
    void uploadMaskToGL(const float* maskData, int w, int h);

    // CPU resize: RGBA bilinear → model input size
    void resizeRGBA(const uint8_t* src, int srcW, int srcH,
                    uint8_t* dst, int dstW, int dstH);

    // Build TFLite Interpreter after model is loaded (HAS_TFLITE only)
    bool buildInterpreter();
};

} // namespace ai
} // namespace video
} // namespace sdk
