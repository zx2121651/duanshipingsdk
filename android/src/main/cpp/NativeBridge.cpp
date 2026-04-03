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
            const char* vsrc = "#version 300 es\n layout(location=0) in vec4 p; layout(location=1) in vec2 tc; out vec2 vtc; void main(){gl_Position=p; vtc=tc;}";
            const char* fsrc = "#version 300 es\n precision mediump float; in vec2 vtc; out vec4 c; uniform sampler2D tex; void main(){c=texture(tex, vtc);}";

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


extern "C" {

JNIEXPORT jlong JNICALL
Java_com_sdk_video_RenderEngine_nativeInit(JNIEnv *env, jobject thiz) {
    EngineWrapper* wrapper = new EngineWrapper();
    wrapper->filterEngine->addFilter(std::make_shared<OES2RGBFilter>());
    wrapper->filterEngine->initialize();
    return reinterpret_cast<jlong>(wrapper);
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
    if (!wrapper || !wrapper->filterEngine) return textureId;

    auto start = std::chrono::high_resolution_clock::now();

    jsize len = env->GetArrayLength(matrix);
    if (len == 16) {
        jfloat *elements = env->GetFloatArrayElements(matrix, 0);
        std::vector<float> textureMatrix(elements, elements + 16);
        env->ReleaseFloatArrayElements(matrix, elements, 0);
        wrapper->filterEngine->updateParameter("textureMatrix", std::any(textureMatrix));
    }

    Texture inTex = {static_cast<uint32_t>(textureId), width, height};
    Texture outTex = wrapper->filterEngine->processFrame(inTex, width, height);

#ifndef WIN32
    wrapper->renderToRecordingSurface(outTex, width, height, timestampNs);
#endif

    auto end = std::chrono::high_resolution_clock::now();
    long long durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    jclass cls = env->GetObjectClass(thiz);
    jfieldID lastFrameTimeMsId = env->GetFieldID(cls, "lastFrameTimeMs", "J");
    if (lastFrameTimeMsId) {
        env->SetLongField(thiz, lastFrameTimeMsId, static_cast<jlong>(durationMs));
    }

    return outTex.id;
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeSetRecordingSurface(JNIEnv *env, jobject thiz, jlong handle, jobject surface) {
#ifndef WIN32
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper) {
        if (surface) {
            wrapper->setupRecordingSurface(env, surface);
        } else {
            wrapper->releaseRecordingSurface();
        }
    }
#endif
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeUpdateParameterFloat(JNIEnv *env, jobject thiz, jlong handle, jstring key, jfloat value) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper) {
        const char *keyStr = env->GetStringUTFChars(key, nullptr);
        wrapper->filterEngine->updateParameter(std::string(keyStr), std::any(value));
        env->ReleaseStringUTFChars(key, keyStr);
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeUpdateParameterInt(JNIEnv *env, jobject thiz, jlong handle, jstring key, jint value) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper) {
        const char *keyStr = env->GetStringUTFChars(key, nullptr);
        wrapper->filterEngine->updateParameter(std::string(keyStr), std::any(value));
        env->ReleaseStringUTFChars(key, keyStr);
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_RenderEngine_nativeUpdateParameterBool(JNIEnv *env, jobject thiz, jlong handle, jstring key, jboolean value) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper) {
        const char *keyStr = env->GetStringUTFChars(key, nullptr);
        bool val = (value == JNI_TRUE);
        wrapper->filterEngine->updateParameter(std::string(keyStr), std::any(val));
        env->ReleaseStringUTFChars(key, keyStr);
    }
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeAddFilter(JNIEnv *env, jobject thiz, jlong handle, jint filterType) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper) {
        FilterPtr filter;
        switch(filterType) {
            case 0: filter = std::make_shared<BrightnessFilter>(); break;
            case 1: filter = std::make_shared<GaussianBlurFilter>(&(wrapper->filterEngine->m_frameBufferPool)); break;
            case 2: filter = std::make_shared<LookupFilter>(); break;
            case 3: filter = std::make_shared<BilateralFilter>(); break;
            case 4: filter = std::make_shared<CinematicLookupFilter>(); break;
#ifdef __ANDROID__
            case 5:
                // 【智能路由器 / Fallback】:
                // 如果 UI 请求了最高性能的 Compute Blur，但在初始化阶段通过 GLContextManager
                // 嗅探出该设备不支持 GLES 3.1 或最大工作组 (Max Invocations) 小于 256（易引发驱动内核崩溃），
                // 此时执行【无感降级】：拦截请求，将其路由到我们之前写好的普通 Two-pass 高斯模糊。
                // 业务层的 API 调用不会失败，且视觉效果几乎一致，仅仅是算力降维。
                if (wrapper->filterEngine->getContextManager().isComputeShaderSupported()) {
                    filter = std::make_shared<ComputeBlurFilter>();
                } else {
                    filter = std::make_shared<GaussianBlurFilter>(&(wrapper->filterEngine->m_frameBufferPool));
                }
                break;
#endif
            default: return -2; // Unknown filter type
        }
        if (filter) {
            wrapper->filterEngine->addFilter(filter);
            return 0;
        }
    }
    return -1; // Null wrapper
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeRemoveAllFilters(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper) {
        wrapper->filterEngine->removeAllFilters();
        wrapper->filterEngine->addFilter(std::make_shared<OES2RGBFilter>());
        return 0;
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeStartAudioRecord(JNIEnv *env, jobject thiz, jlong handle, jint sampleRate) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper && wrapper->audioEngine) {
        bool ok = wrapper->audioEngine->start(sampleRate);
        return ok ? 0 : -2;
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_RenderEngine_nativeStopAudioRecord(JNIEnv *env, jobject thiz, jlong handle) {
    EngineWrapper* wrapper = reinterpret_cast<EngineWrapper*>(handle);
    if (wrapper && wrapper->audioEngine) {
        wrapper->audioEngine->stop();
        return 0;
    }
    return -1;
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

} // extern "C"
