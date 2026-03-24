#include <jni.h>
#include <string>
#include <vector>
#include "../../../../core/include/FilterEngine.h"
#include "../../../../core/include/Filters.h"

using namespace sdk::video;

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_sdk_video_VideoProcessor_nativeCreate(JNIEnv *env, jobject thiz) {
    FilterEngine* engine = new FilterEngine();
    // Default Android setup: assume input is OES texture from SurfaceTexture
    engine->addFilter(std::make_shared<OESInputFilter>());
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_VideoProcessor_nativeDestroy(JNIEnv *env, jobject thiz, jlong handle) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    delete engine;
}

JNIEXPORT void JNICALL
Java_com_sdk_video_VideoProcessor_nativeInitialize(JNIEnv *env, jobject thiz, jlong handle) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        engine->initialize();
    }
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_VideoProcessor_nativeProcessFrame(JNIEnv *env, jobject thiz, jlong handle, jint textureId, jint width, jint height, jfloatArray matrix) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (!engine) return textureId;

    // Extract 4x4 transform matrix from Java float array
    jsize len = env->GetArrayLength(matrix);
    if (len == 16) {
        jfloat *elements = env->GetFloatArrayElements(matrix, 0);
        std::vector<float> textureMatrix(elements, elements + 16);
        env->ReleaseFloatArrayElements(matrix, elements, 0);

        // Update the texture matrix on the pipeline
        // (This applies globally to any filter respecting this parameter, specifically OESInputFilter)
        engine->updateParameter("textureMatrix", std::any(textureMatrix));
    }

    Texture inTex = {static_cast<uint32_t>(textureId), width, height};
    Texture outTex = engine->processFrame(inTex, width, height);
    return outTex.id;
}

JNIEXPORT void JNICALL
Java_com_sdk_video_VideoProcessor_nativeUpdateParameterFloat(JNIEnv *env, jobject thiz, jlong handle, jstring key, jfloat value) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        const char *keyStr = env->GetStringUTFChars(key, nullptr);
        std::string k(keyStr);
        engine->updateParameter(k, std::any(value));
        env->ReleaseStringUTFChars(key, keyStr);
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_VideoProcessor_nativeUpdateParameterInt(JNIEnv *env, jobject thiz, jlong handle, jstring key, jint value) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        const char *keyStr = env->GetStringUTFChars(key, nullptr);
        std::string k(keyStr);
        engine->updateParameter(k, std::any(value));
        env->ReleaseStringUTFChars(key, keyStr);
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_VideoProcessor_nativeAddFilter(JNIEnv *env, jobject thiz, jlong handle, jint filterType) {
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
Java_com_sdk_video_VideoProcessor_nativeRemoveAllFilters(JNIEnv *env, jobject thiz, jlong handle) {
    FilterEngine* engine = reinterpret_cast<FilterEngine*>(handle);
    if (engine) {
        engine->removeAllFilters();
        // Re-add mandatory OES input filter for Android
        engine->addFilter(std::make_shared<OESInputFilter>());
    }
}

}
