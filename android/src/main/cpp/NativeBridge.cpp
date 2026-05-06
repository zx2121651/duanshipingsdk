#include <android/log.h>
#include <jni.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

#ifndef WIN32
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#endif


#include <android/asset_manager_jni.h>
#include "AndroidAssetProvider.h"
#include "../../../../core/include/FilterEngine.h"
#include "../../../../core/include/Filters.h"
#include "../../../../core/include/timeline/Compositor.h"
#include "../../../../core/include/ai/FaceLandmarkDetector.h"
#include "../../../../core/include/ai/FaceReshapeFilter.h"
#include "../../../../core/include/ai/MakeupFilter.h"
#include "../../../../core/include/ai/HairSegmentationFilter.h"
#include "../../../../core/include/EffectPlugin.h"
#include "OboeAudioEngine.h"

using namespace sdk::video;

struct EngineWrapper {
    std::shared_ptr<FilterEngine> filterEngine;
    std::unique_ptr<OboeAudioEngine> audioEngine;

    // DSR 配置：存储待注入到 Compositor 的参数
    sdk::video::timeline::DsrConfig pendingDsr;
    bool   dsrEnabled   = false;
    float  lastDsrScale = 1.0f;  // 最近一次 export 结束时的实际倍率

    // ── 抖音对标：人脸 AI 模块 ──────────────────────────────────────
    std::shared_ptr<ai::FaceLandmarkDetector> faceLandmark;
    std::shared_ptr<FaceReshapeFilter>        faceReshape;
    std::shared_ptr<MakeupFilter>             makeup;
    std::shared_ptr<HairSegmentationFilter>   hairSeg;
    EffectPluginManager                       effectManager;

#ifndef WIN32
    // Recording state
    ANativeWindow* recordingWindow = nullptr;
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLSurface recordingSurface = EGL_NO_SURFACE;
    EGLContext sharedContext = EGL_NO_CONTEXT;

    // Passthrough shader for rendering to MediaCodec surface
    GLuint recordProgram = 0;
#endif

    EngineWrapper() {
        filterEngine = std::make_shared<FilterEngine>();
        audioEngine = std::make_unique<OboeAudioEngine>();
    }

    ~EngineWrapper() {
#ifndef WIN32
        releaseRecordingSurface();
        glDeleteProgram(recordProgram);
        recordProgram = 0;
#endif
    }

#ifndef WIN32
    bool setupRecordingSurface(JNIEnv* env, jobject surface) {
        if (!surface) return false;

        // Cleanup existing surface before overwriting
        releaseRecordingSurface();

        recordingWindow = ANativeWindow_fromSurface(env, surface);
        if (!recordingWindow) return false;

        // Current context from GLSurfaceView or TextureView
        eglDisplay = eglGetCurrentDisplay();
        sharedContext = eglGetCurrentContext();

        if (eglDisplay == EGL_NO_DISPLAY || sharedContext == EGL_NO_CONTEXT) {
            ANativeWindow_release(recordingWindow);
            recordingWindow = nullptr;
            return false;
        }

        // Create EGL window surface for MediaCodec input
        EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_RECORDABLE_ANDROID, 1,
            EGL_NONE
        };

        EGLint numConfigs;
        EGLConfig config;
        eglChooseConfig(eglDisplay, configAttribs, &config, 1, &numConfigs);

        EGLint surfaceAttribs[] = {EGL_NONE};
        recordingSurface = eglCreateWindowSurface(eglDisplay, config, recordingWindow, surfaceAttribs);

        // Compile simple passthrough program if not done
        if (recordProgram == 0) {
            std::string vsrc_str = "";
            std::string fsrc_str = "";

            if (filterEngine && filterEngine->getShaderManager()) {
                vsrc_str = filterEngine->getShaderManager()->getShaderSource("record_passthrough.vert");
                fsrc_str = filterEngine->getShaderManager()->getShaderSource("record_passthrough.frag");
            }

            const char* vsrc = vsrc_str.c_str();
            const char* fsrc = fsrc_str.c_str();

            auto compile = [](GLenum type, const char* s) {
                GLuint sh = glCreateShader(type); glShaderSource(sh, 1, &s, NULL); glCompileShader(sh); return sh;
            };
            GLuint vs = compile(GL_VERTEX_SHADER, vsrc);
            GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
            recordProgram = glCreateProgram();
            glAttachShader(recordProgram, vs); glAttachShader(recordProgram, fs);
            glLinkProgram(recordProgram);
        }
        return true;
    }

    void releaseRecordingSurface() {
        if (eglDisplay != EGL_NO_DISPLAY && recordingSurface != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay, recordingSurface);
            recordingSurface = EGL_NO_SURFACE;
        }
        if (recordingWindow) {
            ANativeWindow_release(recordingWindow);
            recordingWindow = nullptr;
        }
    }

    void renderToRecordingSurface(Texture tex, int width, int height, int64_t timestampNs) {
        if (recordingSurface == EGL_NO_SURFACE || eglDisplay == EGL_NO_DISPLAY) return;

        // Save current draw surface
        EGLSurface drawSurface = eglGetCurrentSurface(EGL_DRAW);
        EGLSurface readSurface = eglGetCurrentSurface(EGL_READ);

        // Make recording surface current
        if (!eglMakeCurrent(eglDisplay, recordingSurface, recordingSurface, sharedContext)) {
            return;
        }

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(recordProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex.id);

        // Android recording requires fixing vertical orientation typically
        static const float p[] = {-1,-1, 1,-1, -1,1, 1,1};
        static const float t[] = {0,0, 1,0, 0,1, 1,1};

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, p);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, t);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        typedef EGLBoolean (EGLAPIENTRYP PFNEGLPRESENTATIONTIMEANDROIDPROC)(EGLDisplay dpy, EGLSurface sur, khronos_stime_nanoseconds_t time);
        static PFNEGLPRESENTATIONTIMEANDROIDPROC eglPresentationTimeANDROID =
            (PFNEGLPRESENTATIONTIMEANDROIDPROC)eglGetProcAddress("eglPresentationTimeANDROID");

        if (eglPresentationTimeANDROID) {
            eglPresentationTimeANDROID(eglDisplay, recordingSurface, timestampNs);
        }

        eglSwapBuffers(eglDisplay, recordingSurface);

        // Restore original display surface
        eglMakeCurrent(eglDisplay, drawSurface, readSurface, sharedContext);
    }
#endif
};


static jfieldID g_lastFrameTimeMsId = nullptr;


void throwNativeException(JNIEnv* env, int errorCode, const std::string& message) {
    jclass exClass = env->FindClass("com/sdk/video/NativeRenderException");
    if (exClass != nullptr) {
        // Find constructor NativeRenderException(int, String)
        jmethodID constructor = env->GetMethodID(exClass, "<init>", "(ILjava/lang/String;)V");
        if (constructor != nullptr) {
            jstring jmsg = env->NewStringUTF(message.c_str());
            jobject exObj = env->NewObject(exClass, constructor, errorCode, jmsg);
            if (exObj != nullptr) {
                env->Throw(reinterpret_cast<jthrowable>(exObj));
            }
            env->DeleteLocalRef(jmsg);
            env->DeleteLocalRef(exObj);
        }
        env->DeleteLocalRef(exClass);
    }
}

/**
 * Unified error reporting to Kotlin via onNativeRenderError callback.
 * Ensures JNI local references are correctly managed.
 */
void reportErrorToKotlin(JNIEnv* env, jobject thiz, int errorCode, const std::string& message) {
    jclass cls = env->GetObjectClass(thiz);
    if (!cls) return;

    // Look for the @Keep annotated guardian method in Kotlin
    jmethodID mid = env->GetMethodID(cls, "onNativeRenderError", "(ILjava/lang/String;)V");
    if (mid != nullptr) {
        jstring jmsg = env->NewStringUTF(message.c_str());
        env->CallVoidMethod(thiz, mid, errorCode, jmsg);
        env->DeleteLocalRef(jmsg);
    }
    env->DeleteLocalRef(cls);
}


extern "C" {

JNIEXPORT jlong JNICALL
Java_com_sdk_video_RenderEngine_nativeInit(JNIEnv *env, jobject thiz, jobject assetManager) {
    if (!g_lastFrameTimeMsId) {
        jclass cls = env->GetObjectClass(thiz);
        g_lastFrameTimeMsId = env->GetFieldID(cls, "lastFrameTimeMs", "J");
        env->DeleteLocalRef(cls);
    }

    std::unique_ptr<EngineWrapper> wrapper;
    try {
        wrapper = std::make_unique<EngineWrapper>();
    } catch (const std::bad_alloc& e) {
        throwNativeException(env, static_cast<int>(ErrorCode::ERR_INIT_CONTEXT_FAILED), "Native Memory Allocation Failed");
        return 0;
    }

    if (assetManager) {
        AAssetManager* nativeAssetManager = AAssetManager_fromJava(env, assetManager);
        auto assetProvider = std::make_shared<AndroidAssetProvider>(nativeAssetManager);
        wrapper->filterEngine->setAssetProvider(assetProvider);
    }

    auto oesFilter = std::make_shared<OES2RGBFilter>();
    auto addRes = wrapper->filterEngine->addFilterRaw(oesFilter);
    if (!addRes.isOk()) {
        throwNativeException(env, addRes.getErrorCode(), "Failed to add initial OES filter: " + addRes.getMessage());
        return 0;
    }

    auto res = wrapper->filterEngine->initialize();
    if (!res.isOk()) {
        throwNativeException(env, res.getErrorCode(), res.getMessage());
        return 0;
    }
    return reinterpret_cast<jlong>(wrapper.release());
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeRelease(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper) {
        wrapper->filterEngine->release();
        delete wrapper;
    }
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeProcessFrame(JNIEnv *env, jobject thiz, jlong handle, jint textureId, jint width, jint height, jfloatArray matrix, jlong timestampNs) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) {
        reportErrorToKotlin(env, thiz, static_cast<int>(ErrorCode::ERR_INIT_CONTEXT_FAILED),
                           "Engine Critical Failure: FilterEngine is null or destroyed. Halted.");
        return -1;
    }

    auto start = std::chrono::high_resolution_clock::now();

    jsize len = env->GetArrayLength(matrix);
    if (len == 16) {
        jfloat buffer[16];
        env->GetFloatArrayRegion(matrix, 0, 16, buffer);
        wrapper->filterEngine->updateParameterMat4("textureMatrix", buffer);
    }

    Texture inTex = {static_cast<uint32_t>(textureId), static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    // Unified ResultPayload unpacking and error propagation
    auto result = wrapper->filterEngine->processFrame(inTex, width, height);
    if (!result.isOk()) {
        __android_log_print(ANDROID_LOG_ERROR, "NativeBridge", "Frame rendering failed: [%d] %s",
                           result.getErrorCode(), result.getMessage().c_str());

        reportErrorToKotlin(env, thiz, result.getErrorCode(), result.getMessage());
        return result.getErrorCode();
    }

    Texture outTex = result.getValue();
    if (outTex.id == 0) {
        reportErrorToKotlin(env, thiz, static_cast<int>(ErrorCode::ERR_RENDER_INVALID_STATE),
                           "Pipeline produced an invalid texture ID (0).");
        return static_cast<int>(ErrorCode::ERR_RENDER_INVALID_STATE);
    }

#ifndef WIN32
    wrapper->renderToRecordingSurface(outTex, width, height, timestampNs);
#endif

    auto end = std::chrono::high_resolution_clock::now();
    long long durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (g_lastFrameTimeMsId) {
        env->SetLongField(thiz, g_lastFrameTimeMsId, static_cast<jlong>(durationMs));
    }

    return static_cast<jint>(outTex.id);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeSetRecordingSurface(JNIEnv *env, jobject thiz, jlong handle, jobject surface) {
#ifndef WIN32
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Context not initialized");
        return;
    }

    if (surface) {
        if (!wrapper->setupRecordingSurface(env, surface)) {
            throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Failed to setup recording surface (EGL Context missing?)");
        }
    } else {
        wrapper->releaseRecordingSurface();
    }
#else
    throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Not supported on WIN32");
#endif
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeUpdateParameterFloat(JNIEnv *env, jobject thiz, jlong handle, jstring key, jfloat value) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Engine not initialized");
        return;
    }

    const char *keyStr = env->GetStringUTFChars(key, nullptr);
    wrapper->filterEngine->updateParameter(std::string(keyStr), std::any(value));
    env->ReleaseStringUTFChars(key, keyStr);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeUpdateParameterInt(JNIEnv *env, jobject thiz, jlong handle, jstring key, jint value) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Engine not initialized");
        return;
    }

    const char *keyStr = env->GetStringUTFChars(key, nullptr);
    wrapper->filterEngine->updateParameter(std::string(keyStr), std::any(value));
    env->ReleaseStringUTFChars(key, keyStr);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeUpdateParameterBool(JNIEnv *env, jobject thiz, jlong handle, jstring key, jboolean value) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Engine not initialized");
        return;
    }

    const char *keyStr = env->GetStringUTFChars(key, nullptr);
    bool val = (value == JNI_TRUE);
    wrapper->filterEngine->updateParameter(std::string(keyStr), std::any(val));
    env->ReleaseStringUTFChars(key, keyStr);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeAddFilter(JNIEnv *env, jobject thiz, jlong handle, jint filterType) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Engine not initialized");
        return;
    }

    auto res = wrapper->filterEngine->addFilter(static_cast<sdk::video::FilterType>(filterType));
    if (!res.isOk()) {
        throwNativeException(env, res.getErrorCode(), res.getMessage());
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeRemoveAllFilters(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Engine not initialized");
        return;
    }

    auto res1 = wrapper->filterEngine->removeAllFilters();
    if (!res1.isOk()) {
        throwNativeException(env, res1.getErrorCode(), res1.getMessage());
        return;
    }
    auto res2 = wrapper->filterEngine->addFilterRaw(std::make_shared<OES2RGBFilter>());
    if (!res2.isOk()) {
        throwNativeException(env, res2.getErrorCode(), res2.getMessage());
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeStartAudioRecord(JNIEnv *env, jobject thiz, jlong handle, jint sampleRate) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->audioEngine) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Engine not initialized");
        return;
    }
    bool ok = wrapper->audioEngine->start(sampleRate);
    if (!ok) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_OBOE_FAILED), "Failed to start audio record");
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeStopAudioRecord(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->audioEngine) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Engine not initialized");
        return;
    }
    wrapper->audioEngine->stop();
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeReadAudioPCM(JNIEnv *env, jobject thiz, jlong handle, jbyteArray buffer, jint length) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->audioEngine) return 0;

    jbyte* nativeBuffer = env->GetByteArrayElements(buffer, nullptr);
    int32_t bytesRead = wrapper->audioEngine->readPCM(reinterpret_cast<uint8_t*>(nativeBuffer), length);
    env->ReleaseByteArrayElements(buffer, nativeBuffer, 0);

    return bytesRead;
}

JNIEXPORT jlong JNICALL
Java_com_sdk_video_RenderEngine_nativeGetAudioTimeNs(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->audioEngine) return 0;

    return static_cast<jlong>(wrapper->audioEngine->getAudioTimeNs());
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeUpdateShaderSource(JNIEnv *env, jobject thiz, jlong handle, jstring name, jstring source) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) {
        throwNativeException(env, static_cast<int>(sdk::video::ERR_INIT_CONTEXT_FAILED), "Engine not initialized");
        return;
    }

    const char* nameStr = env->GetStringUTFChars(name, nullptr);
    const char* sourceStr = env->GetStringUTFChars(source, nullptr);

    auto res = wrapper->filterEngine->updateShaderSource(nameStr, sourceStr);

    env->ReleaseStringUTFChars(name, nameStr);
    env->ReleaseStringUTFChars(source, sourceStr);

    if (!res.isOk()) {
        throwNativeException(env, res.getErrorCode(), res.getMessage());
    }
}

JNIEXPORT jfloatArray JNICALL
Java_com_sdk_video_RenderEngine_nativeGetMetrics(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) return nullptr;

    auto metrics = wrapper->filterEngine->getPerformanceMetrics();
    jfloatArray result = env->NewFloatArray(5);
    if (result == nullptr) return nullptr;

    jfloat fill[5];
    fill[0] = metrics.averageFrameTimeMs;
    fill[1] = metrics.p50FrameTimeMs;
    fill[2] = metrics.p90FrameTimeMs;
    fill[3] = metrics.p99FrameTimeMs;
    fill[4] = static_cast<jfloat>(metrics.droppedFrames);

    env->SetFloatArrayRegion(result, 0, 5, fill);
    return result;
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeRecordDroppedFrame(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper && wrapper->filterEngine) {
        wrapper->filterEngine->recordDroppedFrame();
    }
}

// P1-6: GL Context Lost / Restored recovery
JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeOnContextLost(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper && wrapper->filterEngine) {
        wrapper->filterEngine->onContextLost();
    }
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeOnContextRestored(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) {
        return static_cast<jint>(sdk::video::ErrorCode::ERR_RENDER_NOT_INITIALIZED);
    }
    auto res = wrapper->filterEngine->onContextRestored();
    return static_cast<jint>(res.getErrorCode());
}

// ---------------------------------------------------------------------------
// P2-14: onTrimMemory — Android ComponentCallbacks2 内存压力响应
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeOnTrimMemory(JNIEnv *env, jobject thiz, jlong handle, jint level)
{
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper && wrapper->filterEngine) {
        wrapper->filterEngine->onTrimMemory(static_cast<int>(level));
    }
}

// ---------------------------------------------------------------------------
// P2-13: DSR (Dynamic Resolution Scaling) JNI
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeSetDsrConfig(
        JNIEnv *env, jobject thiz, jlong handle,
        jfloat targetFps, jfloat minScale, jfloat maxScale)
{
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper) return;
    wrapper->pendingDsr.targetFps      = static_cast<float>(targetFps);
    wrapper->pendingDsr.minScaleFactor = static_cast<float>(minScale);
    wrapper->pendingDsr.maxScaleFactor = static_cast<float>(maxScale);
    wrapper->dsrEnabled = true;
    wrapper->lastDsrScale = static_cast<float>(maxScale);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeDisableDsr(JNIEnv *env, jobject thiz, jlong handle)
{
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper) return;
    wrapper->dsrEnabled   = false;
    wrapper->lastDsrScale = 1.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_sdk_video_RenderEngine_nativeGetDsrScale(JNIEnv *env, jobject thiz, jlong handle)
{
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    return wrapper ? static_cast<jfloat>(wrapper->lastDsrScale) : 1.0f;
}

// ============================================================
// 人脸 AI — 关键点检测
// ============================================================

JNIEXPORT jboolean JNICALL
Java_com_sdk_video_RenderEngine_nativeLoadFaceLandmarkModel(
    JNIEnv* env, jobject, jlong handle, jstring modelPath)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (!w) return JNI_FALSE;
    if (!w->faceLandmark)
        w->faceLandmark = std::make_shared<ai::FaceLandmarkDetector>();
    const char* path = env->GetStringUTFChars(modelPath, nullptr);
    bool ok = w->faceLandmark->loadModel(path);
    env->ReleaseStringUTFChars(modelPath, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloatArray JNICALL
Java_com_sdk_video_RenderEngine_nativeGetFaceLandmarks(
    JNIEnv* env, jobject, jlong handle)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (!w || !w->faceLandmark) return nullptr;
    auto result = w->faceLandmark->getLatestResult();
    if (result.faceCount == 0) return nullptr;
    // 返回 float[106*2]：x0,y0,x1,y1,...
    const int N = ai::kFaceLandmarkCount * 2;
    jfloatArray arr = env->NewFloatArray(N);
    jfloat pts[N];
    for (int i = 0; i < ai::kFaceLandmarkCount; ++i) {
        pts[i*2+0] = result.faces[0].landmarks[i].x;
        pts[i*2+1] = result.faces[0].landmarks[i].y;
    }
    env->SetFloatArrayRegion(arr, 0, N, pts);
    return arr;
}

// ============================================================
// 人脸重塑（大眼/瘦脸/V脸/瘦鼻/额头）
// ============================================================

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeSetFaceReshape(
    JNIEnv*, jobject, jlong handle,
    jfloat eyeScale, jfloat faceSlim, jfloat noseSlim,
    jfloat foreheadUp, jfloat chinV, jfloat mouthWidth)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (!w) return;
    if (!w->faceReshape) {
        w->faceReshape = std::make_shared<FaceReshapeFilter>();
        w->faceReshape->initialize();
    }
    w->faceReshape->setEyeScale   (eyeScale);
    w->faceReshape->setFaceSlim   (faceSlim);
    w->faceReshape->setNoseSlim   (noseSlim);
    w->faceReshape->setForeheadUp (foreheadUp);
    w->faceReshape->setChinV      (chinV);
    w->faceReshape->setMouthWidth (mouthWidth);
}

// ============================================================
// 美妆（口红/腮红/眼影/高光/修容/眉毛）
// ============================================================

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeSetMakeup(
    JNIEnv* env, jobject, jlong handle, jint layer,
    jfloat r, jfloat g, jfloat b, jfloat intensity)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (!w) return;
    if (!w->makeup) {
        w->makeup = std::make_shared<MakeupFilter>();
        w->makeup->initialize();
    }
    switch (layer) {
        case 0: w->makeup->setLipColor  (r, g, b, intensity); break;
        case 1: w->makeup->setBlush     (r, g, b, intensity); break;
        case 2: w->makeup->setEyeshadow (r, g, b, intensity); break;
        case 3: w->makeup->setHighlight (intensity);          break;
        case 4: w->makeup->setContour   (intensity);          break;
        case 5: w->makeup->setEyebrow   (r, g, b, intensity); break;
    }
}

// ============================================================
// 发色染色
// ============================================================

JNIEXPORT jboolean JNICALL
Java_com_sdk_video_RenderEngine_nativeLoadHairModel(
    JNIEnv* env, jobject, jlong handle, jstring modelPath)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (!w) return JNI_FALSE;
    if (!w->hairSeg) {
        w->hairSeg = std::make_shared<HairSegmentationFilter>();
        w->hairSeg->initialize();
    }
    const char* path = env->GetStringUTFChars(modelPath, nullptr);
    bool ok = w->hairSeg->loadModel(path);
    env->ReleaseStringUTFChars(modelPath, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeSetHairColor(
    JNIEnv*, jobject, jlong handle, jfloat r, jfloat g, jfloat b,
    jfloat colorIntensity, jfloat glossIntensity)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (!w || !w->hairSeg) return;
    w->hairSeg->setHairColor(r, g, b);
    w->hairSeg->setColorIntensity(colorIntensity);
    w->hairSeg->setGlossIntensity(glossIntensity);
}

// ============================================================
// 特效包
// ============================================================

JNIEXPORT jstring JNICALL
Java_com_sdk_video_RenderEngine_nativeLoadEffect(
    JNIEnv* env, jobject, jlong handle, jstring effectRoot)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (!w) return env->NewStringUTF("");
    const char* root = env->GetStringUTFChars(effectRoot, nullptr);
    std::string effectId = w->effectManager.loadEffect(root);
    env->ReleaseStringUTFChars(effectRoot, root);
    return env->NewStringUTF(effectId.c_str());
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeActivateEffect(
    JNIEnv* env, jobject, jlong handle, jstring effectId)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (!w) return;
    const char* id = env->GetStringUTFChars(effectId, nullptr);
    w->effectManager.activateEffect(id);
    env->ReleaseStringUTFChars(effectId, id);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeDeactivateAllEffects(
    JNIEnv*, jobject, jlong handle)
{
    EngineWrapper* w = reinterpret_cast<EngineWrapper*>(handle);
    if (w) w->effectManager.deactivateAll();
}

// ============================================================
// RHI 后端选择 API
// ============================================================

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeSetBackend(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jint backendType)
{
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) return;
    wrapper->filterEngine->setPreferredBackend(
        static_cast<sdk::video::rhi::BackendType>(backendType));
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeGetActiveBackend(
    JNIEnv* env, jobject /*thiz*/, jlong handle)
{
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) return 1; // GLES fallback
    return static_cast<jint>(wrapper->filterEngine->getActiveBackend());
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeGetGLESVersion(
    JNIEnv* env, jobject /*thiz*/, jlong handle)
{
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (!wrapper || !wrapper->filterEngine) return 30;
    return static_cast<jint>(
        wrapper->filterEngine->getContextManager().getGLESVersionInt());
}

} // extern "C"
