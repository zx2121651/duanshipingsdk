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
#include "OboeAudioEngine.h"

using namespace sdk::video;

struct EngineWrapper {
    std::shared_ptr<FilterEngine> filterEngine;
    std::unique_ptr<OboeAudioEngine> audioEngine;

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
    void setupRecordingSurface(JNIEnv* env, jobject surface) {
        if (!surface) return;

        recordingWindow = ANativeWindow_fromSurface(env, surface);
        if (!recordingWindow) return;

        // Current context from GLSurfaceView or TextureView
        eglDisplay = eglGetCurrentDisplay();
        sharedContext = eglGetCurrentContext();

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
        jclass cls = env->GetObjectClass(thiz);
        // 寻找 Kotlin 层特别标注了 @Keep 的警戒方法
        jmethodID mid = env->GetMethodID(cls, "onNativeRenderError", "(ILjava/lang/String;)V");
        if (mid != nullptr) {
            jstring jmsg = env->NewStringUTF("Engine Critical Failure: FilterEngine is null or destroyed. Halted.");
            // 主动上报错误码 -1001
            env->CallVoidMethod(thiz, mid, -1001, jmsg);
            env->DeleteLocalRef(jmsg);
        }
        env->DeleteLocalRef(cls);
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
    auto result = wrapper->filterEngine->processFrame(inTex, width, height);
    if (!result.isOk()) {
        __android_log_print(ANDROID_LOG_ERROR, "NativeBridge", "Frame rendering failed: [%d] %s", result.getErrorCode(), result.getMessage().c_str());

        jclass cls = env->GetObjectClass(thiz);
        jmethodID mid = env->GetMethodID(cls, "onNativeRenderError", "(ILjava/lang/String;)V");
        if (mid != nullptr) {
            jstring jmsg = env->NewStringUTF(result.getMessage().c_str());
            env->CallVoidMethod(thiz, mid, result.getErrorCode(), jmsg);
            env->DeleteLocalRef(jmsg);
        }
        env->DeleteLocalRef(cls);
        return result.getErrorCode();
    }

    Texture outTex = result.getValue();

#ifndef WIN32
    wrapper->renderToRecordingSurface(outTex, width, height, timestampNs);
#endif

    auto end = std::chrono::high_resolution_clock::now();
    long long durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (g_lastFrameTimeMsId) {
        env->SetLongField(thiz, g_lastFrameTimeMsId, static_cast<jlong>(durationMs));
    }

    return outTex.id;
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
        wrapper->setupRecordingSurface(env, surface);
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

    wrapper->filterEngine->updateShaderSource(nameStr, sourceStr);

    env->ReleaseStringUTFChars(name, nameStr);
    env->ReleaseStringUTFChars(source, sourceStr);
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

} // extern "C"
