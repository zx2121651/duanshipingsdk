#include <jni.h>
#include <string>
#include "../../../../core/include/FilterEngine.h"
#include "../../../../core/include/Filters.h"

using namespace sdk::video;

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_sdk_video_FilterEngine_nativeCreate(JNIEnv *env, jobject thiz) {
    FilterEngine* engine = new FilterEngine();
    // Default Android setup: assume input is OES texture from SurfaceTexture
    engine->addFilter(std::make_shared<OESInputFilter>());
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_FilterEngine_nativeDestroy(JNIEnv *env, jobject thiz, jlong handle) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    delete engine;
}

JNIEXPORT void JNICALL
Java_com_sdk_video_FilterEngine_nativeInitialize(JNIEnv *env, jobject thiz, jlong handle) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        engine->initialize();
    }
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_FilterEngine_nativeProcessFrame(JNIEnv *env, jobject thiz, jlong handle, jint textureId, jint width, jint height) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        Texture inTex = {static_cast<uint32_t>(textureId), width, height};
        Texture outTex = engine->processFrame(inTex, width, height);
        return outTex.id;
    }
    return textureId;
}

JNIEXPORT void JNICALL
Java_com_sdk_video_FilterEngine_nativeUpdateParameterFloat(JNIEnv *env, jobject thiz, jlong handle, jstring key, jfloat value) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        const char *keyStr = env->GetStringUTFChars(key, nullptr);
        std::string k(keyStr);
        engine->updateParameter(k, std::any(value));
        env->ReleaseStringUTFChars(key, keyStr);
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_FilterEngine_nativeUpdateParameterInt(JNIEnv *env, jobject thiz, jlong handle, jstring key, jint value) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        const char *keyStr = env->GetStringUTFChars(key, nullptr);
        std::string k(keyStr);
        engine->updateParameter(k, std::any(value));
        env->ReleaseStringUTFChars(key, keyStr);
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_FilterEngine_nativeAddFilter(JNIEnv *env, jobject thiz, jlong handle, jint filterType) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        FilterPtr filter;
        switch(filterType) {
            case 0: filter = std::make_shared<BrightnessFilter>(); break;
            case 1: filter = std::make_shared<GaussianBlurFilter>(); break;
            case 2: filter = std::make_shared<LookupFilter>(); break;
            default: break;
        }
        if (filter) {
            engine->addFilter(filter);
        }
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_FilterEngine_nativeRemoveAllFilters(JNIEnv *env, jobject thiz, jlong handle) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        engine->removeAllFilters();
        // Re-add mandatory OES input filter for Android
        engine->addFilter(std::make_shared<OESInputFilter>());
    }
}

}
