/**
 * TfliteInferenceEngine.cpp
 *
 * 完整实现（HAS_TFLITE）：
 *   - 加载 .tflite 模型 → TFLite Interpreter
 *   - 自动启用 GPU Delegate（HAS_TFLITE_GPU_DELEGATE + Android）
 *   - 推理：RGBA resize → float input → Invoke() → float mask
 *   - 上传 mask 为 GL_R8 纹理
 *
 * Stub 实现（无 HAS_TFLITE）：
 *   - loadModel() 返回 false + 错误描述
 *   - runInference() 返回 success=false
 *   - 其他方法空实现
 */

#include "../../include/ai/TfliteInferenceEngine.h"
#include "../../include/GLStateManager.h"

#define LOG_TAG "TfliteInferenceEngine"
#include "../../include/Log.h"

#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>

#ifdef HAS_TFLITE
#   include "tensorflow/lite/c/c_api.h"
#   ifdef HAS_TFLITE_GPU_DELEGATE
#       include "tensorflow/lite/delegates/gpu/delegate.h"
#   endif
#endif

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// Impl (Pimpl — 隔离 TFLite 头文件污染)
// ---------------------------------------------------------------------------
struct TfliteInferenceEngine::Impl {
#ifdef HAS_TFLITE
    TfLiteModel*              model = nullptr;
    TfLiteInterpreterOptions* options = nullptr;
    TfLiteInterpreter*        interpreter = nullptr;
#   ifdef HAS_TFLITE_GPU_DELEGATE
    TfLiteDelegate* gpuDelegate = nullptr;
#   endif
    std::vector<uint8_t> modelBuffer; // for loadModelFromBuffer()
#endif
};

// ---------------------------------------------------------------------------
TfliteInferenceEngine::TfliteInferenceEngine()
    : m_impl(std::make_unique<Impl>()) {}

TfliteInferenceEngine::~TfliteInferenceEngine() {
    release();
}

// ---------------------------------------------------------------------------
// loadModel()
// ---------------------------------------------------------------------------
bool TfliteInferenceEngine::loadModel(const std::string& modelPath) {
#ifdef HAS_TFLITE
    m_impl->model = TfLiteModelCreateFromFile(modelPath.c_str());
    if (!m_impl->model) {
        m_lastError = "TfliteInferenceEngine: cannot load model from " + modelPath;
        LOGE("%s", m_lastError.c_str());
        return false;
    }
    return buildInterpreter();
#else
    m_lastError = "TfliteInferenceEngine: TFLite not compiled (HAS_TFLITE not defined). "
                  "Add prebuilt libtensorflowlite.so and set -DTFLITE_PREBUILT_DIR.";
    LOGW("%s", m_lastError.c_str());
    return false;
#endif
}

bool TfliteInferenceEngine::loadModelFromBuffer(const void* modelData, size_t modelSize) {
#ifdef HAS_TFLITE
    m_impl->modelBuffer.assign(
        static_cast<const uint8_t*>(modelData),
        static_cast<const uint8_t*>(modelData) + modelSize);
    m_impl->model = TfLiteModelCreate(m_impl->modelBuffer.data(), m_impl->modelBuffer.size());
    if (!m_impl->model) {
        m_lastError = "TfliteInferenceEngine: cannot build model from buffer";
        return false;
    }
    return buildInterpreter();
#else
    (void)modelData; (void)modelSize;
    m_lastError = "TfliteInferenceEngine: TFLite not compiled";
    return false;
#endif
}

// ---------------------------------------------------------------------------
// buildInterpreter() — 内部帮助函数
// ---------------------------------------------------------------------------
#ifdef HAS_TFLITE
bool TfliteInferenceEngine::buildInterpreter() {
    m_impl->options = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(m_impl->options, 2);

#   ifdef HAS_TFLITE_GPU_DELEGATE
    TfLiteGpuDelegateOptionsV2 opts = TfLiteGpuDelegateOptionsV2Default();
    opts.inference_preference = TFLITE_GPU_INFERENCE_PREFERENCE_FAST_SINGLE_ANSWER;
    opts.inference_priority1  = TFLITE_GPU_INFERENCE_PRIORITY_MIN_LATENCY;
    m_impl->gpuDelegate = TfLiteGpuDelegateV2Create(&opts);
    if (m_impl->gpuDelegate) {
        TfLiteInterpreterOptionsAddDelegate(m_impl->options, m_impl->gpuDelegate);
        LOGI("TfliteInferenceEngine: GPU delegate enabled");
    } else {
        LOGW("TfliteInferenceEngine: GPU delegate failed, falling back to CPU");
    }
#   endif

    m_impl->interpreter = TfLiteInterpreterCreate(m_impl->model, m_impl->options);
    if (!m_impl->interpreter) {
        m_lastError = "TfliteInferenceEngine: TfLiteInterpreterCreate failed";
        return false;
    }

    if (TfLiteInterpreterAllocateTensors(m_impl->interpreter) != kTfLiteOk) {
        m_lastError = "TfliteInferenceEngine: AllocateTensors failed";
        return false;
    }

    const TfLiteTensor* inputTensor = TfLiteInterpreterGetInputTensor(m_impl->interpreter, 0);
    if (TfLiteTensorNumDims(inputTensor) >= 3) {
        m_inputH = TfLiteTensorDim(inputTensor, 1);
        m_inputW = TfLiteTensorDim(inputTensor, 2);
    }

    m_loaded = true;
    LOGI("TfliteInferenceEngine: model loaded, input=%dx%d", m_inputW, m_inputH);
    return true;
}
#endif // HAS_TFLITE

// ---------------------------------------------------------------------------
// runInference()
// ---------------------------------------------------------------------------
InferenceResult TfliteInferenceEngine::runInference(
    const uint8_t* inputPixels, int inputW, int inputH)
{
    InferenceResult result;
#ifdef HAS_TFLITE
    if (!m_loaded || !m_impl->interpreter) {
        result.errorMessage = "TfliteInferenceEngine: model not loaded";
        return result;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    // 1. Resize 输入到模型尺寸
    std::vector<uint8_t> resized(static_cast<size_t>(m_inputW * m_inputH * 4));
    resizeRGBA(inputPixels, inputW, inputH, resized.data(), m_inputW, m_inputH);

    // 2. 填充输入 tensor（float32，归一化到 [0,1]）
    TfLiteTensor* inputTensor = TfLiteInterpreterGetInputTensor(m_impl->interpreter, 0);
    float* inputData = (float*)TfLiteTensorData(inputTensor);
    const int numPixels = m_inputW * m_inputH;
    for (int i = 0; i < numPixels; ++i) {
        inputData[i * 3 + 0] = resized[i * 4 + 0] / 255.0f;
        inputData[i * 3 + 1] = resized[i * 4 + 1] / 255.0f;
        inputData[i * 3 + 2] = resized[i * 4 + 2] / 255.0f;
    }

    // 3. 推理
    if (TfLiteInterpreterInvoke(m_impl->interpreter) != kTfLiteOk) {
        result.errorMessage = "TfliteInferenceEngine: Invoke() failed";
        return result;
    }

    // 4. 读取输出（selfie-segmentation 输出为 float[1][H][W][1] or [1][H][W][2]）
    const TfLiteTensor* outputTensor = TfLiteInterpreterGetOutputTensor(m_impl->interpreter, 0);
    const float* outputData = (const float*)TfLiteTensorData(outputTensor);
    int outChannels = TfLiteTensorDim(outputTensor, TfLiteTensorNumDims(outputTensor) - 1);

    // 提取前景通道（index 1 for 2-channel, index 0 for 1-channel）
    int fgChannel = (outChannels >= 2) ? 1 : 0;
    std::vector<float> mask(static_cast<size_t>(m_inputW * m_inputH));
    for (int i = 0; i < m_inputW * m_inputH; ++i) {
        float v = outputData[i * outChannels + fgChannel];
        // 2-channel: softmax → [bg, fg], apply sigmoid to fg channel if needed
        if (outChannels >= 2) {
            float bg = outputData[i * outChannels + 0];
            float fg = outputData[i * outChannels + 1];
            // softmax
            float maxV = std::max(bg, fg);
            float eBg = std::exp(bg - maxV);
            float eFg = std::exp(fg - maxV);
            v = eFg / (eBg + eFg);
        } else {
            // sigmoid
            v = 1.0f / (1.0f + std::exp(-v));
        }
        mask[i] = std::max(0.0f, std::min(1.0f, v));
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.inferenceTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    // 5. 上传 mask 为 GL_R8 纹理
    ensureMaskTexture(m_inputW, m_inputH);
    uploadMaskToGL(mask.data(), m_inputW, m_inputH);

    result.success       = true;
    result.maskTextureId = m_maskTextureId;
    result.maskWidth     = m_inputW;
    result.maskHeight    = m_inputH;

    LOGV("TfliteInferenceEngine: inference %.1fms, mask tex=%u", result.inferenceTimeMs, m_maskTextureId);
    return result;

#else
    (void)inputPixels; (void)inputW; (void)inputH;
    result.errorMessage = "TfliteInferenceEngine: TFLite not compiled — stub";
    return result;
#endif
}

// ---------------------------------------------------------------------------
// runInference() — Texture Overload (GPU Downsampling)
// ---------------------------------------------------------------------------
InferenceResult TfliteInferenceEngine::runInference(GLuint textureId, int width, int height) {
    InferenceResult result;
#ifdef HAS_TFLITE
    if (!m_loaded || !m_impl->interpreter) {
        result.errorMessage = "TfliteInferenceEngine: model not loaded";
        return result;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    // 1. Ensure downsample FBO and texture are ready
    ensureDownsampleResources(m_inputW, m_inputH);

    // 2. Perform GPU downsampling using glBlitFramebuffer
    GLuint srcFbo = 0;
    glGenFramebuffers(1, &srcFbo);
    
    GLStateManager::getInstance().bindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);

    GLStateManager::getInstance().bindFramebuffer(GL_DRAW_FRAMEBUFFER, m_downsampleFbo);

    glBlitFramebuffer(0, 0, width, height, 0, 0, m_inputW, m_inputH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // 3. Read back the downsampled pixels from m_downsampleFbo (low-res readback)
    GLStateManager::getInstance().bindFramebuffer(GL_READ_FRAMEBUFFER, m_downsampleFbo);
    glReadPixels(0, 0, m_inputW, m_inputH, GL_RGBA, GL_UNSIGNED_BYTE, m_downsampleBuffer.data());

    // Clean up temporary FBO binding
    GLStateManager::getInstance().bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GLStateManager::getInstance().bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &srcFbo);

    // 4. Fill TFLite float input tensor
    TfLiteTensor* inputTensor = TfLiteInterpreterGetInputTensor(m_impl->interpreter, 0);
    float* inputData = (float*)TfLiteTensorData(inputTensor);
    const int numPixels = m_inputW * m_inputH;
    for (int i = 0; i < numPixels; ++i) {
        inputData[i * 3 + 0] = m_downsampleBuffer[i * 4 + 0] / 255.0f;
        inputData[i * 3 + 1] = m_downsampleBuffer[i * 4 + 1] / 255.0f;
        inputData[i * 3 + 2] = m_downsampleBuffer[i * 4 + 2] / 255.0f;
    }

    // 5. Invoke Inference
    if (TfLiteInterpreterInvoke(m_impl->interpreter) != kTfLiteOk) {
        result.errorMessage = "TfliteInferenceEngine: Invoke() failed";
        return result;
    }

    // 6. Decode output
    const TfLiteTensor* outputTensor = TfLiteInterpreterGetOutputTensor(m_impl->interpreter, 0);
    const float* outputData = (const float*)TfLiteTensorData(outputTensor);
    int outChannels = TfLiteTensorDim(outputTensor, TfLiteTensorNumDims(outputTensor) - 1);

    int fgChannel = (outChannels >= 2) ? 1 : 0;
    std::vector<float> mask(static_cast<size_t>(m_inputW * m_inputH));
    for (int i = 0; i < m_inputW * m_inputH; ++i) {
        float v = outputData[i * outChannels + fgChannel];
        if (outChannels >= 2) {
            float bg = outputData[i * outChannels + 0];
            float fg = outputData[i * outChannels + 1];
            float maxV = std::max(bg, fg);
            float eBg = std::exp(bg - maxV);
            float eFg = std::exp(fg - maxV);
            v = eFg / (eBg + eFg);
        } else {
            v = 1.0f / (1.0f + std::exp(-v));
        }
        mask[i] = std::max(0.0f, std::min(1.0f, v));
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.inferenceTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    // 7. Upload mask to GL_R8 texture
    ensureMaskTexture(m_inputW, m_inputH);
    uploadMaskToGL(mask.data(), m_inputW, m_inputH);

    result.success       = true;
    result.maskTextureId = m_maskTextureId;
    result.maskWidth     = m_inputW;
    result.maskHeight    = m_inputH;

    LOGV("TfliteInferenceEngine: GPU-downsampled inference %.1fms, mask tex=%u", result.inferenceTimeMs, m_maskTextureId);
    return result;
#else
    (void)textureId; (void)width; (void)height;
    result.errorMessage = "TfliteInferenceEngine: TFLite not compiled — stub";
    return result;
#endif
}

// ---------------------------------------------------------------------------
// ensureDownsampleResources()
// ---------------------------------------------------------------------------
void TfliteInferenceEngine::ensureDownsampleResources(int w, int h) {
    if (m_downsampleInited && m_downsampleTexId != 0 && m_downsampleFbo != 0) {
        return;
    }
    
    glGenFramebuffers(1, &m_downsampleFbo);
    glGenTextures(1, &m_downsampleTexId);
    
    GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, m_downsampleFbo);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_downsampleTexId);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_downsampleTexId, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("TfliteInferenceEngine: downsample framebuffer incomplete: %u", status);
    }
    
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, 0);
    GLStateManager::getInstance().bindFramebuffer(GL_FRAMEBUFFER, 0);
    
    m_downsampleBuffer.resize(w * h * 4);
    m_downsampleInited = true;
}

// ---------------------------------------------------------------------------
// ensureMaskTexture() — 懒初始化 GL_R8 mask 纹理
// ---------------------------------------------------------------------------
void TfliteInferenceEngine::ensureMaskTexture(int w, int h) {
    if (m_maskTexInited && m_maskTextureId != 0) return;

    glGenTextures(1, &m_maskTextureId);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_maskTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    m_maskTexInited = true;
}

// ---------------------------------------------------------------------------
// uploadMaskToGL()
// ---------------------------------------------------------------------------
void TfliteInferenceEngine::uploadMaskToGL(const float* maskData, int w, int h) {
    GLStateManager::getInstance().activeTexture(GL_TEXTURE0);
    GLStateManager::getInstance().bindTexture(GL_TEXTURE_2D, m_maskTextureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_FLOAT, maskData);
}

// ---------------------------------------------------------------------------
// resizeRGBA() — 双线性缩放（CPU，仅在推理前调用一次）
// ---------------------------------------------------------------------------
void TfliteInferenceEngine::resizeRGBA(
    const uint8_t* src, int srcW, int srcH,
    uint8_t* dst, int dstW, int dstH)
{
    float xScale = static_cast<float>(srcW) / dstW;
    float yScale = static_cast<float>(srcH) / dstH;

    for (int dy = 0; dy < dstH; ++dy) {
        float sy = (dy + 0.5f) * yScale - 0.5f;
        int   y0 = static_cast<int>(sy);
        int   y1 = std::min(y0 + 1, srcH - 1);
        float fy = sy - y0;
        y0 = std::max(y0, 0);

        for (int dx = 0; dx < dstW; ++dx) {
            float sx = (dx + 0.5f) * xScale - 0.5f;
            int   x0 = static_cast<int>(sx);
            int   x1 = std::min(x0 + 1, srcW - 1);
            float fx = sx - x0;
            x0 = std::max(x0, 0);

            for (int c = 0; c < 4; ++c) {
                float v = (1-fy) * ((1-fx) * src[(y0*srcW+x0)*4+c] + fx * src[(y0*srcW+x1)*4+c])
                        +    fy  * ((1-fx) * src[(y1*srcW+x0)*4+c] + fx * src[(y1*srcW+x1)*4+c]);
                dst[(dy*dstW+dx)*4+c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v)));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// releaseGLResources() / release()
// ---------------------------------------------------------------------------
void TfliteInferenceEngine::releaseGLResources() {
    if (m_maskTextureId != 0) {
        glDeleteTextures(1, &m_maskTextureId);
        m_maskTextureId = 0;
        m_maskTexInited = false;
    }
    if (m_downsampleTexId != 0) {
        glDeleteTextures(1, &m_downsampleTexId);
        m_downsampleTexId = 0;
    }
    if (m_downsampleFbo != 0) {
        glDeleteFramebuffers(1, &m_downsampleFbo);
        m_downsampleFbo = 0;
    }
    m_downsampleInited = false;
}

void TfliteInferenceEngine::release() {
    releaseGLResources();
#ifdef HAS_TFLITE
    if (m_impl) {
        if (m_impl->interpreter) {
            TfLiteInterpreterDelete(m_impl->interpreter);
            m_impl->interpreter = nullptr;
        }
#   ifdef HAS_TFLITE_GPU_DELEGATE
        if (m_impl->gpuDelegate) {
            TfLiteGpuDelegateV2Delete(m_impl->gpuDelegate);
            m_impl->gpuDelegate = nullptr;
        }
#   endif
        if (m_impl->options) {
            TfLiteInterpreterOptionsDelete(m_impl->options);
            m_impl->options = nullptr;
        }
        if (m_impl->model) {
            TfLiteModelDelete(m_impl->model);
            m_impl->model = nullptr;
        }
    }
#endif
    m_loaded = false;
}

} // namespace ai
} // namespace video
} // namespace sdk
