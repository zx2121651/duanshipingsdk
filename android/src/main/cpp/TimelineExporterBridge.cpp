#include "../../../../core/include/GLTypes.h"
#include <jni.h>
#include <string>
#include <memory>
#include "../../../../core/include/timeline/TimelineExporter.h"
#include "../../../../core/include/timeline/Compositor.h"
#include "../../../../core/include/timeline/AudioMixer.h"
#include "../../../../core/include/timeline/AudioDecoder.h"

using namespace sdk::video::timeline;

/**
 * 离线导出器 JNI 桥接层
 */

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_sdk_video_timeline_TimelineExporter_nativeCreate(JNIEnv *env, jobject thiz) {
    auto exporter = TimelineExporter::create();
    return reinterpret_cast<jlong>(exporter.release());
}

JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelineExporter_nativeRelease(JNIEnv *env, jobject thiz, jlong handle) {
    delete reinterpret_cast<TimelineExporter*>(handle);
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineExporter_nativeConfigure(JNIEnv *env, jobject thiz, jlong handle, jstring outputPath, jint width, jint height, jint fps, jint bitrate) {
    auto exporter = reinterpret_cast<TimelineExporter*>(handle);
    if (!exporter) return -1;
    const char *path = env->GetStringUTFChars(outputPath, nullptr);
    auto res = exporter->configure(path, width, height, fps, bitrate);
    env->ReleaseStringUTFChars(outputPath, path);
    return res.getErrorCode();
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineExporter_nativeExportAsync(JNIEnv *env, jobject thiz, jlong handle, jlong timelineHandle, jlong engineHandle, jobject onProgress, jobject onComplete) {
    auto exporter = reinterpret_cast<TimelineExporter*>(handle);
    if (!exporter) return -1;

    // timelineHandle is std::shared_ptr<Timeline>*
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(timelineHandle);
    // engineHandle is EngineWrapper* which contains shared_ptr<FilterEngine>
    // However, the Compositor needs a shared_ptr<FilterEngine>.
    // In NativeBridge.cpp, EngineWrapper is:
    // struct EngineWrapper { std::shared_ptr<FilterEngine> filterEngine; ... }
    // EngineWrapper is defined in NativeBridge.cpp — replicate its layout here.
    // Only fields needed for export are accessed; dsrEnabled/pendingDsr are new.
    struct EngineWrapper {
        std::shared_ptr<sdk::video::FilterEngine> filterEngine;
        void* audioEngine = nullptr;         // OboeAudioEngine* — not used here
        sdk::video::timeline::DsrConfig pendingDsr;
        bool  dsrEnabled   = false;
        float lastDsrScale = 1.0f;
    };
    auto wrapper = reinterpret_cast<EngineWrapper*>(engineHandle);

    if (!timelinePtr || !(*timelinePtr) || !wrapper || !wrapper->filterEngine) return -1;

    auto compositor = std::make_shared<Compositor>(*timelinePtr, wrapper->filterEngine);
    // 将 RenderEngine 中待命的 DSR 配置注入 Compositor
    if (wrapper->dsrEnabled) {
        compositor->setDsrConfig(wrapper->pendingDsr);
    }

    // 创建音频混音器（可选）：有平台解码器池时注入，无则静音导出
    {
        auto audioPool = sdk::video::timeline::createPlatformAudioDecoderPool();
        if (audioPool) {
            auto audioMixer = std::make_shared<sdk::video::timeline::AudioMixer>(
                *timelinePtr, audioPool);
            exporter->setAudioMixer(audioMixer);
        }
    }

    // JNI Callbacks management
    JavaVM* jvm;
    env->GetJavaVM(&jvm);
    jobject progressGlobal = env->NewGlobalRef(onProgress);
    jobject completeGlobal = env->NewGlobalRef(onComplete);

    // Capture compositor + wrapper so we can write back the final DSR scale
    auto compositorWeak = std::weak_ptr<Compositor>(compositor);
    auto res = exporter->exportAsync(*timelinePtr, compositor,
        [jvm, progressGlobal](float progress) {
            JNIEnv* myEnv;
            if (jvm->AttachCurrentThread(&myEnv, nullptr) == JNI_OK) {
                jclass cls = myEnv->GetObjectClass(progressGlobal);
                jmethodID mid = myEnv->GetMethodID(cls, "onProgress", "(F)V");
                myEnv->CallVoidMethod(progressGlobal, mid, progress);
                myEnv->DeleteLocalRef(cls);
                jvm->DetachCurrentThread();
            }
        },
        [jvm, completeGlobal, progressGlobal, compositorWeak, wrapper](sdk::video::Result result) {
            // Write back actual DSR scale so nativeGetDsrScale() reflects real performance
            if (auto c = compositorWeak.lock()) {
                wrapper->lastDsrScale = c->getDsrScale();
            }
            JNIEnv* myEnv;
            if (jvm->AttachCurrentThread(&myEnv, nullptr) == JNI_OK) {
                jclass cls = myEnv->GetObjectClass(completeGlobal);
                jmethodID mid = myEnv->GetMethodID(cls, "onComplete", "(ILjava/lang/String;)V");
                jstring msg = myEnv->NewStringUTF(result.getMessage().c_str());
                myEnv->CallVoidMethod(completeGlobal, mid, result.getErrorCode(), msg);
                myEnv->DeleteLocalRef(msg);
                myEnv->DeleteLocalRef(cls);

                // Cleanup globals
                myEnv->DeleteGlobalRef(completeGlobal);
                myEnv->DeleteGlobalRef(progressGlobal);

                jvm->DetachCurrentThread();
            }
        }
    );

    return res.getErrorCode();
}

JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelineExporter_nativeCancel(JNIEnv *env, jobject thiz, jlong handle) {
    auto exporter = reinterpret_cast<TimelineExporter*>(handle);
    if (exporter) exporter->cancel();
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineExporter_nativeGetState(JNIEnv *env, jobject thiz, jlong handle) {
    auto exporter = reinterpret_cast<TimelineExporter*>(handle);
    return exporter ? static_cast<int>(exporter->getState()) : 0;
}

JNIEXPORT jfloat JNICALL
Java_com_sdk_video_timeline_TimelineExporter_nativeGetProgress(JNIEnv *env, jobject thiz, jlong handle) {
    auto exporter = reinterpret_cast<TimelineExporter*>(handle);
    return exporter ? exporter->getProgress() : 0.0f;
}

} // extern "C"
